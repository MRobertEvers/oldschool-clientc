/**
 * The GLES2 renderer core: the context, the programs, the world texture atlas,
 * every retained CPU/GPU vertex buffer, the per-frame index stream and the
 * command dispatch.
 *
 * What it deliberately does not know is how the world's triangles get ordered.
 * There are two implementations of that, they share nothing with each other,
 * and each lives in its own translation unit:
 *
 *   platform_renderer_gles2_painter.c   painter's algorithm
 *   platform_renderer_gles2_zbuffer.c   hardware depth test
 *
 * ToriRS_GLES2_Init picks one by creating (or not creating) the depth
 * implementation's state. ::zbuffer is that state and doubles as the selector.
 * See platform_renderer_gles2_core.h for the contract and for what the
 * GLES2 ceiling turned into here.
 *
 * The retained model is the D3D9 renderer's, kept on purpose (see
 * platform_win32_renderer_d3d9_core.c): two arena groups (STATIC, retained
 * across frames; DYNAMIC, refilled every frame for actors), plus Batch16 for
 * the scene build, whose pages are the pages the U16 index stream addresses.
 */

#include "platform/platform_renderer_gles2_core.h"

#include "engine/boot_bar.h"
#include "log/torirs_log.h"
#include "perf/torirs_perf.h"
#include "platform/platform_renderer_gles2_shaders.h"

#include "core/trspk_math.h"
#include "toridraw.h"
#include "toridraw_element_id.h"
#include "toridraw_math.h"
#include "painters/painters.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One line of the TORIRS_GLES2_DEBUG census. TORIRS_REPORT rather than
 * TORIRS_LOG: the reader asked for it by setting the variable, so it must
 * survive an optimized build. The lane decides where stderr goes -- the
 * console on the desktop and in the browser, logcat on Android. */
#define gles2_report_line(fmt, ...) TORIRS_REPORT(fmt "\n", __VA_ARGS__)

_Static_assert(
    GLES2_ATLAS_COLS * TRSPK_ATLAS_TILE == GLES2_ATLAS_DIM,
    "the atlas grid must tile the atlas exactly");
_Static_assert(
    GLES2_ATLAS_COLS * GLES2_ATLAS_COLS == GLES2_ATLAS_SLOTS,
    "the slot count is the grid squared");
_Static_assert(
    sizeof(struct TRSPK_VertexGLES2) == 28u,
    "the world vertex layout is what the attribute pointers describe");
_Static_assert(
    sizeof(struct GLES2VertexUI) == 28u,
    "the UI vertex layout is what the attribute pointers describe");
_Static_assert(
    sizeof(struct GLES2VertexRotmask) == 32u,
    "the rotmask vertex layout is what the attribute pointers describe");
_Static_assert(
    GLES2_ATTRIB_TEXINFO == GLES2_ATTRIB_MASK_TEXCOORD,
    "the fourth attribute slot is shared: world texinfo or rotmask mask uv");

enum GLES2StreamLayout
{
    GLES2_STREAM_NONE = 0,
    GLES2_STREAM_WORLD = 1,
    GLES2_STREAM_UI = 2,
    GLES2_STREAM_ROTMASK = 3,
};

#define GLES2_VERTEX_STRIDE ((GLsizei)sizeof(struct TRSPK_VertexGLES2))

/* ---- cached GL state ------------------------------------------------------ */

void
gles2_set_blend(struct ToriRS_GLES2* renderer, bool enabled)
{
    assert(renderer);
    if( renderer->blend_on == enabled )
        return;
    if( enabled )
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
    renderer->blend_on = enabled;
}

void
gles2_set_depth(struct ToriRS_GLES2* renderer, bool test, bool write)
{
    assert(renderer);
    if( renderer->depth_test_on != test )
    {
        if( test )
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
        renderer->depth_test_on = test;
    }
    if( renderer->depth_write_on != write )
    {
        glDepthMask(write ? GL_TRUE : GL_FALSE);
        renderer->depth_write_on = write;
    }
}

void
gles2_set_cull(struct ToriRS_GLES2* renderer, bool enabled)
{
    assert(renderer);
    if( renderer->cull_on == enabled )
        return;
    if( enabled )
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
    renderer->cull_on = enabled;
}

void
gles2_set_scissor(struct ToriRS_GLES2* renderer, const struct GLES2Rect* rect)
{
    assert(renderer);
    if( !rect )
    {
        if( renderer->scissor_on )
        {
            glDisable(GL_SCISSOR_TEST);
            renderer->scissor_on = false;
        }
        return;
    }
    if( !renderer->scissor_on )
    {
        glEnable(GL_SCISSOR_TEST);
        renderer->scissor_on = true;
    }
    if( memcmp(&renderer->scissor_rect, rect, sizeof(*rect)) != 0 )
    {
        glScissor(rect->x, rect->y, rect->width, rect->height);
        renderer->scissor_rect = *rect;
    }
}

void
gles2_bind_texture0(struct ToriRS_GLES2* renderer, GLuint texture)
{
    assert(renderer);
    if( renderer->bound_texture0 == texture )
        return;
    glBindTexture(GL_TEXTURE_2D, texture);
    renderer->bound_texture0 = texture;
}

/*
 * Warm the per-element lines the painter dispatch reads for the commands
 * behind this one, one line class per step so each step reads only what
 * the step before it fetched: the pose table's element row at +3, its
 * track's vertex-base array at +2, and the static batch's entry (the
 * chunk, offset and vertex count) at +1. The frame's emit loop resolves
 * the element ids three commands ahead for its own prefetches; this is the
 * renderer's half of the same pipeline. Measured before it existed: the
 * batch entry read alone was 39% of gles2_dispatch.
 */
void
gles2_prefetch_ahead_ids(
    struct ToriRS_GLES2* renderer,
    int id_plus1,
    int id_plus2,
    int id_plus3)
{
    const struct TRSPK_PoseTable* table = &renderer->batch_poses;
    int id;

    assert(renderer);
    if( !table->elements || !renderer->has_3d )
        return;

    id = id_plus3;
    if( id >= 0 )
    {
        uint32_t const index = (uint32_t)ToriDraw_ElementIndexOfRaw(id);
        if( index < table->element_count )
            __builtin_prefetch(&table->elements[index], 0, 1);
    }
    id = id_plus2;
    if( id >= 0 )
    {
        uint32_t const index = (uint32_t)ToriDraw_ElementIndexOfRaw(id);
        if( index < table->element_count )
        {
            const struct TRSPK_PoseTrack* track = &table->elements[index].tracks[0];
            if( track->vertex_base )
                __builtin_prefetch(track->vertex_base, 0, 1);
        }
    }
    id = id_plus1;
    if( id >= 0 )
    {
        uint32_t const index = (uint32_t)ToriDraw_ElementIndexOfRaw(id);
        if( index < table->element_count )
        {
            const struct TRSPK_PoseTrack* track = &table->elements[index].tracks[0];
            if( track->vertex_base && track->pose_count > 0u )
            {
                uint32_t const base = track->vertex_base[0];
                if( base != TRSPK_POSE_VERTEX_BASE_INVALID && (base & GLES2_BATCH_POSE_FLAG) )
                {
                    uint32_t const slot =
                        (base >> GLES2_BATCH_POSE_SLOT_SHIFT) & GLES2_BATCH_POSE_SLOT_MASK;
                    if( slot < renderer->static_batch_count )
                    {
                        const struct GLES2StaticBatch* batch = &renderer->static_batches[slot];
                        if( batch->active && batch->cpu )
                        {
                            const struct TRSPK_Batch16Entry* entry = trspk_batch16_get_entry(
                                batch->cpu, base & GLES2_BATCH_POSE_ENTRY_MASK);
                            if( entry )
                                __builtin_prefetch(entry, 0, 1);
                        }
                    }
                }
            }
        }
    }
}

static void
gles2_prefetch_ahead(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_Frame* frame)
{
    assert(renderer);
    assert(frame);
    if( !renderer->batch_poses.elements || !renderer->has_3d )
        return;
    gles2_prefetch_ahead_ids(
        renderer,
        ToriRS_FrameLookaheadElementId(frame, 1),
        ToriRS_FrameLookaheadElementId(frame, 2),
        ToriRS_FrameLookaheadElementId(frame, 3));
}

void
gles2_bind_texture1(struct ToriRS_GLES2* renderer, GLuint texture)
{
    assert(renderer);
    if( renderer->bound_texture1 == texture )
        return;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texture);
    glActiveTexture(GL_TEXTURE0);
    renderer->bound_texture1 = texture;
}

void
gles2_use_program(struct ToriRS_GLES2* renderer, const struct GLES2Program* program)
{
    assert(renderer);
    assert(program);
    if( renderer->current_program == program )
        return;
    glUseProgram(program->id);
    renderer->current_program = program;
}

void
gles2_bind_array_buffer(struct ToriRS_GLES2* renderer, GLuint buffer)
{
    if( renderer->bound_array_buffer == buffer )
        return;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    renderer->bound_array_buffer = buffer;
}

/* Put GL into a known state and make the cache agree with it. Every frame
 * starts here: the context is shared with nothing, but the cost is a dozen
 * calls and it makes a stale cache impossible rather than unlikely. */
static void
gles2_state_reset(struct ToriRS_GLES2* renderer)
{
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DITHER);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    renderer->blend_on = false;
    renderer->depth_test_on = false;
    renderer->depth_write_on = true;
    renderer->cull_on = false;
    renderer->scissor_on = false;
    memset(&renderer->scissor_rect, 0, sizeof(renderer->scissor_rect));
    renderer->bound_texture0 = 0u;
    renderer->bound_texture1 = 0u;
    renderer->bound_array_buffer = 0u;
    renderer->current_program = NULL;
    renderer->stream_buffer = 0u;
    renderer->stream_byte_offset = 0u;
    renderer->stream_layout = GLES2_STREAM_NONE;
}

/* ---- programs --------------------------------------------------------------- */

bool
gles2_check_error(const char* where)
{
    GLenum error = glGetError();
    if( error == GL_NO_ERROR )
        return true;
    TORIRS_ERR("GLES2: %s: glGetError 0x%x\n", where, (unsigned)error);
    return false;
}

static GLuint
gles2_compile_shader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;
    if( shader == 0u )
    {
        TORIRS_ERR("GLES2: glCreateShader failed\n");
        return 0u;
    }
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char log[1024];
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        TORIRS_ERR("GLES2: shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0u;
    }
    return shader;
}

static bool
gles2_link_program(
    struct GLES2Program* program,
    const char* vertex_source,
    const char* fragment_source,
    bool has_mask_attribute,
    bool has_texinfo_attribute,
    const char* label)
{
    GLuint vertex_shader = gles2_compile_shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment_shader = gles2_compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    GLint ok = 0;

    memset(program, 0, sizeof(*program));
    if( vertex_shader == 0u || fragment_shader == 0u )
    {
        if( vertex_shader )
            glDeleteShader(vertex_shader);
        if( fragment_shader )
            glDeleteShader(fragment_shader);
        TORIRS_ERR("GLES2: %s: shaders did not compile\n", label);
        return false;
    }
    program->id = glCreateProgram();
    if( program->id == 0u )
    {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        TORIRS_ERR("GLES2: %s: glCreateProgram failed\n", label);
        return false;
    }
    glAttachShader(program->id, vertex_shader);
    glAttachShader(program->id, fragment_shader);
    /* Before the link, so every program agrees on where each attribute
     * lives and a program switch never re-enables arrays. */
    glBindAttribLocation(program->id, GLES2_ATTRIB_POSITION, "a_position");
    glBindAttribLocation(program->id, GLES2_ATTRIB_TEXCOORD, "a_texcoord");
    glBindAttribLocation(program->id, GLES2_ATTRIB_COLOR, "a_color");
    if( has_texinfo_attribute )
        glBindAttribLocation(program->id, GLES2_ATTRIB_TEXINFO, "a_texinfo");
    if( has_mask_attribute )
        glBindAttribLocation(program->id, GLES2_ATTRIB_MASK_TEXCOORD, "a_mask_texcoord");
    glLinkProgram(program->id);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    glGetProgramiv(program->id, GL_LINK_STATUS, &ok);
    if( !ok )
    {
        char log[1024];
        glGetProgramInfoLog(program->id, (GLsizei)sizeof(log), NULL, log);
        TORIRS_ERR("GLES2: %s: link failed: %s\n", label, log);
        glDeleteProgram(program->id);
        program->id = 0u;
        return false;
    }
    program->u_matrix = glGetUniformLocation(program->id, "u_matrix");
    program->u_clock = glGetUniformLocation(program->id, "u_clock");
    program->u_texture = glGetUniformLocation(program->id, "s_texture");
    program->u_mask = glGetUniformLocation(program->id, "s_mask");
    program->u_mask_invert = glGetUniformLocation(program->id, "u_mask_invert");
    /* Sampler bindings never change: unit 0 is the texture, unit 1 the mask. */
    glUseProgram(program->id);
    if( program->u_texture >= 0 )
        glUniform1i(program->u_texture, 0);
    if( program->u_mask >= 0 )
        glUniform1i(program->u_mask, 1);
    glUseProgram(0);
    return gles2_check_error(label);
}

static void
gles2_delete_program(struct GLES2Program* program)
{
    if( program->id )
        glDeleteProgram(program->id);
    memset(program, 0, sizeof(*program));
}

static bool
gles2_create_programs(struct ToriRS_GLES2* renderer)
{
    /* Fresh program objects hold no uniform values yet. */
    renderer->ui_projection_pushed = false;
    renderer->rotmask_projection_pushed = false;
    return gles2_link_program(
               &renderer->program_world_plain,
               gles2_world_vertex_shader,
               gles2_world_plain_fragment_shader,
               false,
               true,
               "world (plain)") &&
        gles2_link_program(
               &renderer->program_world_cutout,
               gles2_world_vertex_shader,
               gles2_world_cutout_fragment_shader,
               false,
               true,
               "world (cutout)") &&
        gles2_link_program(
               &renderer->program_ui,
               gles2_ui_vertex_shader,
               gles2_ui_fragment_shader,
               true,
               true,
               "ui") &&
        gles2_link_program(
               &renderer->program_rotmask,
               gles2_rotmask_vertex_shader,
               gles2_rotmask_fragment_shader,
               true,
               false,
               "rotmask");
}

/* ---- letterbox and rectangles ---------------------------------------------- */

static void
gles2_update_letterbox(struct ToriRS_GLES2* renderer)
{
    struct TRSPK_Letterbox box;
    if( renderer->width <= 0 || renderer->height <= 0 || renderer->drawable_width <= 0 ||
        renderer->drawable_height <= 0 )
    {
        renderer->letterbox_x = 0;
        renderer->letterbox_y = 0;
        renderer->letterbox_width = 0;
        renderer->letterbox_height = 0;
        return;
    }
    trspk_compute_letterbox(
        renderer->width,
        renderer->height,
        renderer->drawable_width,
        renderer->drawable_height,
        &box);
    renderer->letterbox_x = box.x;
    renderer->letterbox_y = box.y;
    renderer->letterbox_width = box.w;
    renderer->letterbox_height = box.h;
    trspk_mat4_ortho2d_top_left(
        renderer->projection_2d, 0.0f, (float)renderer->width, (float)renderer->height, 0.0f);
    /* The 2D programs hold the previous matrix until it is pushed again. */
    renderer->ui_projection_pushed = false;
    renderer->rotmask_projection_pushed = false;
}

/* Logical (canvas, y down) -> framebuffer (y up), rounding OUTWARD the way
 * the D3D9 lane does so a fractional scale never clips a pixel row the
 * software lane would have drawn. */
bool
gles2_scissor_rect(
    const struct ToriRS_GLES2* renderer,
    int logical_x,
    int logical_y,
    int logical_width,
    int logical_height,
    struct GLES2Rect* out)
{
    int x0;
    int y0;
    int x1;
    int y1;
    int left;
    int top;
    int right;
    int bottom;

    assert(renderer);
    assert(out);
    if( logical_width <= 0 || logical_height <= 0 || renderer->width <= 0 ||
        renderer->height <= 0 || renderer->letterbox_width <= 0 ||
        renderer->letterbox_height <= 0 )
        return false;
    x0 = gles2_clampi(logical_x, 0, renderer->width);
    y0 = gles2_clampi(logical_y, 0, renderer->height);
    x1 = gles2_clampi(logical_x + logical_width, 0, renderer->width);
    y1 = gles2_clampi(logical_y + logical_height, 0, renderer->height);
    if( x1 <= x0 || y1 <= y0 )
        return false;
    left = renderer->letterbox_x +
        (int)((int64_t)x0 * renderer->letterbox_width / renderer->width);
    top = renderer->letterbox_y +
        (int)((int64_t)y0 * renderer->letterbox_height / renderer->height);
    right = renderer->letterbox_x +
        (int)(((int64_t)x1 * renderer->letterbox_width + renderer->width - 1) /
              renderer->width);
    bottom = renderer->letterbox_y +
        (int)(((int64_t)y1 * renderer->letterbox_height + renderer->height - 1) /
              renderer->height);
    left = gles2_clampi(left, 0, renderer->drawable_width);
    right = gles2_clampi(right, left, renderer->drawable_width);
    top = gles2_clampi(top, 0, renderer->drawable_height);
    bottom = gles2_clampi(bottom, top, renderer->drawable_height);
    if( right <= left || bottom <= top )
        return false;
    out->x = left;
    out->width = right - left;
    /* GL's origin is the bottom-left corner of the surface. */
    out->y = renderer->drawable_height - bottom;
    out->height = bottom - top;
    return true;
}

/* ---- the world texture atlas ---------------------------------------------- */

static void
gles2_decode_texture_rgba(
    const struct ToriDraw_Texture* texture,
    uint32_t tile_size,
    uint8_t* rgba)
{
    uint32_t y;
    assert(texture);
    assert(rgba);
    memset(rgba, 0, (size_t)tile_size * tile_size * 4u);
    if( !texture->texels || texture->width <= 0 || texture->height <= 0 )
        return;
    for( y = 0u; y < tile_size; y++ )
    {
        uint32_t source_y = y * (uint32_t)texture->height / tile_size;
        uint32_t x;
        for( x = 0u; x < tile_size; x++ )
        {
            uint32_t source_x = x * (uint32_t)texture->width / tile_size;
            uint32_t source =
                (uint32_t)texture->texels[source_y * (uint32_t)texture->width + source_x];
            uint8_t* out = rgba + ((size_t)y * tile_size + x) * 4u;
            out[0] = (uint8_t)((source >> 16) & 0xffu);
            out[1] = (uint8_t)((source >> 8) & 0xffu);
            out[2] = (uint8_t)(source & 0xffu);
            out[3] = (uint8_t)((texture->opaque || source != 0u) ? 255u : 0u);
        }
    }
}

void
gles2_reserve_upload_stage(struct ToriRS_GLES2* renderer, size_t needed)
{
    size_t capacity;
    uint8_t* grown;
    assert(renderer);
    if( needed <= renderer->upload_stage_capacity )
        return;
    capacity = renderer->upload_stage_capacity ? renderer->upload_stage_capacity : 65536u;
    while( capacity < needed )
        capacity *= 2u;
    grown = (uint8_t*)realloc(renderer->upload_stage, capacity);
    assert(grown);
    renderer->upload_stage = grown;
    renderer->upload_stage_capacity = capacity;
}

/*
 * Push the changed rectangle of a CPU atlas to its GL texture.
 *
 * Only the merged dirty rectangle goes up, and the rows are packed into a
 * tight staging buffer first because GLES2 has no GL_UNPACK_ROW_LENGTH: a
 * sub-rectangle of a wider source cannot be handed to glTexSubImage2D in
 * place. The first upload allocates the texture from the whole CPU atlas.
 */
static bool
gles2_upload_atlas_texture(
    struct ToriRS_GLES2* renderer,
    struct TRSPK_Atlas* atlas,
    GLuint texture,
    bool* allocated,
    GLenum filter,
    int64_t* out_bytes)
{
    struct TRSPK_AtlasDirtyRect dirty;
    uint32_t y;

    assert(renderer);
    assert(atlas);
    assert(allocated);
    assert(out_bytes);
    *out_bytes = 0;
    if( !trspk_atlas_is_initialized(atlas) || !atlas->pixels || texture == 0u )
        return false;
    gles2_bind_texture0(renderer, texture);
    if( !*allocated )
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            (GLsizei)atlas->width,
            (GLsizei)atlas->height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            atlas->pixels);
        *allocated = true;
        *out_bytes = (int64_t)atlas->width * (int64_t)atlas->height * 4;
        trspk_atlas_clear_dirty(atlas);
        return true;
    }
    if( !trspk_atlas_get_dirty_rect(atlas, &dirty) || dirty.w == 0u || dirty.h == 0u )
    {
        trspk_atlas_clear_dirty(atlas);
        return true;
    }
    gles2_reserve_upload_stage(renderer, (size_t)dirty.w * dirty.h * 4u);
    for( y = 0u; y < dirty.h; y++ )
        memcpy(
            renderer->upload_stage + (size_t)y * dirty.w * 4u,
            atlas->pixels + (size_t)(dirty.y + y) * atlas->stride + (size_t)dirty.x * 4u,
            (size_t)dirty.w * 4u);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        (GLint)dirty.x,
        (GLint)dirty.y,
        (GLsizei)dirty.w,
        (GLsizei)dirty.h,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        renderer->upload_stage);
    *out_bytes = (int64_t)dirty.w * (int64_t)dirty.h * 4;
    trspk_atlas_clear_dirty(atlas);
    return true;
}

bool
gles2_upload_atlas(struct ToriRS_GLES2* renderer)
{
    int64_t bytes = 0;
    assert(renderer);
    if( !renderer->gl_context )
        return false;
    if( !trspk_atlas_is_dirty(&renderer->atlas) && renderer->atlas_texture_allocated )
        return true;
    if( !gles2_upload_atlas_texture(
            renderer,
            &renderer->atlas,
            renderer->atlas_texture,
            &renderer->atlas_texture_allocated,
            GL_NEAREST,
            &bytes) )
        return false;
    if( bytes > 0 )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATLAS_UPLOAD_BYTES, bytes);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATLAS_UPLOADS, 1);
    }
    return true;
}

/** The UI atlas upload lives with the UI, but shares the packed-row path. */
bool
gles2_upload_ui_atlas_texture(struct ToriRS_GLES2* renderer, int64_t* out_bytes);
bool
gles2_upload_ui_atlas_texture(struct ToriRS_GLES2* renderer, int64_t* out_bytes)
{
    assert(renderer);
    return gles2_upload_atlas_texture(
        renderer,
        &renderer->ui_sprite_atlas,
        renderer->ui_sprite_atlas_texture,
        &renderer->ui_sprite_atlas_allocated,
        gles2_ui_filter(renderer),
        out_bytes);
}

int
gles2_texture_slot(struct ToriRS_GLES2* renderer, int tex_id)
{
    int slot;
    assert(renderer);
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return -1;
    slot = renderer->tex_slot_of_id[tex_id];
    if( slot >= 0 )
        return slot;
    if( renderer->tex_slot_next >= GLES2_ATLAS_SLOTS )
    {
        static bool warned;
        if( !warned )
        {
            TORIRS_LOG("GLES2: the 2048x2048 world texture atlas is full\n");
            warned = true;
        }
        return -1;
    }
    slot = (int)renderer->tex_slot_next++;
    renderer->tex_slot_of_id[tex_id] = slot;
    return slot;
}

/* The scroll a texture's faces carry, as the two biased bytes the vertex
 * stores. D3D9's texture-matrix signs: DOWN samples from the negative
 * direction, UP from the positive one. */
static void
gles2_texture_anim_bytes(
    const struct ToriDraw_Texture* texture,
    uint8_t* out_anim_u,
    uint8_t* out_anim_v)
{
    int speed;
    int anim_u = 0;
    int anim_v = 0;
    *out_anim_u = TRSPK_VERTEX_GLES2_ANIM_STILL;
    *out_anim_v = TRSPK_VERTEX_GLES2_ANIM_STILL;
    if( !texture )
        return;
    speed = gles2_clampi(texture->animation_speed, -127, 127);
    switch( texture->animation_direction )
    {
    case TORIDRAW_TEXANIM_DIRECTION_U_DOWN:
        anim_u = -speed;
        break;
    case TORIDRAW_TEXANIM_DIRECTION_U_UP:
        anim_u = speed;
        break;
    case TORIDRAW_TEXANIM_DIRECTION_V_DOWN:
        anim_v = -speed;
        break;
    case TORIDRAW_TEXANIM_DIRECTION_V_UP:
        anim_v = speed;
        break;
    default:
        break;
    }
    *out_anim_u = (uint8_t)(TRSPK_VERTEX_GLES2_ANIM_STILL + anim_u);
    *out_anim_v = (uint8_t)(TRSPK_VERTEX_GLES2_ANIM_STILL + anim_v);
}

static struct ToriDraw_Texture*
gles2_scene_texture(struct ToriRS_GLES2* renderer, int tex_id)
{
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY || !renderer->scene )
        return NULL;
    return ToriDraw_TextureMapGet(
        &ToriDraw_SceneTexState(renderer->scene)->texture_map, tex_id);
}

/*
 * Patch the anim bytes of every baked triangle that samples `tex_id`.
 *
 * Geometry can be baked before its asynchronously requested texture is in the
 * scene map, and at that point its scroll is unknown. When the texture lands
 * this walks the retained buffers once -- the triangle config words say which
 * faces are its -- and rewrites three bytes per corner. UVs are untouched:
 * the wrap and the tile mapping are per fragment, so nothing else about the
 * vertex depended on knowing the texture.
 */
static bool
gles2_refresh_anim_range(
    struct TRSPK_VBO* vbo,
    const struct TRSPK_Triangles* triangles,
    uint32_t vertex_base,
    uint32_t vertex_count,
    int tex_id,
    uint8_t anim_u,
    uint8_t anim_v)
{
    bool changed = false;
    uint32_t vertex_offset;
    uint32_t first_changed = UINT32_MAX;
    uint32_t last_changed = 0u;

    assert(vbo);
    assert(triangles);
    if( vbo->format != TRSPK_VERTEX_FORMAT_GLES2 || !vbo->vertices.as_gles2 ||
        !triangles->config || vertex_count % 3u != 0u || vertex_base > vbo->vertex_count ||
        vertex_count > vbo->vertex_count - vertex_base )
        return false;
    for( vertex_offset = 0u; vertex_offset < vertex_count; vertex_offset += 3u )
    {
        uint32_t vertex_index = vertex_base + vertex_offset;
        uint32_t triangle_index = trspk_triangles_index_from_vertex(vertex_index);
        uint32_t corner;
        if( triangle_index >= triangles->cap ||
            trspk_triangles_get(triangles, triangle_index) != tex_id )
            continue;
        for( corner = 0u; corner < 3u; corner++ )
        {
            struct TRSPK_VertexGLES2* vertex = &vbo->vertices.as_gles2[vertex_index + corner];
            if( vertex->anim_u == anim_u && vertex->anim_v == anim_v )
                continue;
            vertex->anim_u = anim_u;
            vertex->anim_v = anim_v;
            changed = true;
        }
        if( changed )
        {
            if( vertex_index < first_changed )
                first_changed = vertex_index;
            last_changed = vertex_index + 3u;
        }
    }
    if( changed )
        trspk_vbo_mark_dirty_range(vbo, first_changed, last_changed - first_changed);
    return changed;
}

static void
gles2_refresh_texture_animation(struct ToriRS_GLES2* renderer, int tex_id)
{
    const struct ToriDraw_Texture* texture = gles2_scene_texture(renderer, tex_id);
    uint8_t anim_u;
    uint8_t anim_v;
    uint32_t group_index;
    uint32_t batch_slot;

    assert(renderer);
    gles2_texture_anim_bytes(texture, &anim_u, &anim_v);
    for( group_index = 0u; group_index < TRSPK_VBO_GROUP_COUNT; group_index++ )
    {
        struct GLES2ModelGroup* group = &renderer->groups[group_index];
        uint32_t slot_index;
        if( !group->arena || !group->vbo_cpu )
            continue;
        for( slot_index = 0u; slot_index < group->arena->slot_count; slot_index++ )
        {
            const struct TRSPK_ModelSlot* model_slot = &group->arena->slots[slot_index];
            if( !trspk_modelslot_is_alive(model_slot) )
                continue;
            (void)gles2_refresh_anim_range(
                group->vbo_cpu,
                &group->triangles,
                model_slot->vertex_base,
                model_slot->tri_count * 3u,
                tex_id,
                anim_u,
                anim_v);
        }
    }
    for( batch_slot = 0u; batch_slot < renderer->static_batch_count; batch_slot++ )
    {
        struct GLES2StaticBatch* batch = &renderer->static_batches[batch_slot];
        uint32_t chunk_count;
        uint32_t chunk_index;
        if( !batch->cpu || (!batch->active && !batch->building) )
            continue;
        chunk_count = trspk_batch16_chunk_count(batch->cpu);
        for( chunk_index = 0u; chunk_index < chunk_count; chunk_index++ )
        {
            struct TRSPK_Batch16Chunk* chunk = trspk_batch16_get_chunk(batch->cpu, chunk_index);
            if( chunk && chunk->vbo &&
                gles2_refresh_anim_range(
                    chunk->vbo,
                    &chunk->triangles,
                    0u,
                    chunk->vertex_count,
                    tex_id,
                    anim_u,
                    anim_v) )
                renderer->static_batch_upload_pending = true;
        }
    }
}

static bool
gles2_load_texture_object(
    struct ToriRS_GLES2* renderer,
    int tex_id,
    const struct ToriDraw_Texture* texture)
{
    static uint8_t rgba[TRSPK_ATLAS_TILE * TRSPK_ATLAS_TILE * 4u];
    int slot;
    assert(renderer);
    assert(texture);
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY || !texture->texels )
        return false;
    slot = gles2_texture_slot(renderer, tex_id);
    if( slot < 0 )
        return false;
    gles2_decode_texture_rgba(texture, TRSPK_ATLAS_TILE, rgba);
    if( !trspk_atlas_grid_insert_at(
            &renderer->atlas,
            (uint32_t)slot,
            rgba,
            TRSPK_ATLAS_TILE * 4u,
            TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE,
            NULL) )
        return false;
    renderer->tex_resident[slot] = 1u;
    if( texture->animation_direction != TORIDRAW_TEXANIM_DIRECTION_NONE )
        gles2_refresh_texture_animation(renderer, tex_id);
    return true;
}

/** Reserve the slot and, when the scene already holds the texels, upload
 *  them. The slot is what a bake encodes, resident or not. */
int
gles2_ensure_texture(struct ToriRS_GLES2* renderer, int tex_id)
{
    struct ToriDraw_Texture* texture;
    int slot;
    assert(renderer);
    if( tex_id < 0 )
        return -1;
    slot = gles2_texture_slot(renderer, tex_id);
    if( slot < 0 )
        return -1;
    if( renderer->tex_resident[slot] )
        return slot;
    texture = gles2_scene_texture(renderer, tex_id);
    if( texture )
        (void)gles2_load_texture_object(renderer, tex_id, texture);
    return slot;
}

static void
gles2_unload_texture(struct ToriRS_GLES2* renderer, int tex_id)
{
    int slot;
    assert(renderer);
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return;
    slot = renderer->tex_slot_of_id[tex_id];
    if( slot >= 0 && (uint32_t)slot < GLES2_ATLAS_SLOTS && renderer->atlas.pixels )
    {
        struct TRSPK_AtlasTile tile;
        /* A deferred widget-model draw samples the world atlas when it is
         * ISSUED, not when it was recorded; clearing this tile now would
         * reach the GPU on the next atlas upload, ahead of that draw. Issue
         * what is recorded first. */
        if( renderer->in2d )
            gles2_ui_flush(renderer);
        if( trspk_atlas_grid_tile_for_slot(&renderer->atlas, (uint32_t)slot, &tile) )
        {
            (void)trspk_atlas_clear_rect(&renderer->atlas, tile.x, tile.y, tile.w, tile.h);
            renderer->tex_resident[slot] = 0u;
        }
    }
}

/* Vertex-level tile mapping, for the transient widget-model triangles that
 * go through the UI program and so get no per-fragment wrap. The clamp to the
 * tile interior is the fixed-function D3D9 rule. */
void
gles2_map_atlas_uv(int slot, float local_u, float local_v, float* out_u, float* out_v)
{
    const float cell = (float)TRSPK_ATLAS_TILE / (float)GLES2_ATLAS_DIM;
    unsigned int index = slot < 0 ? 0u : (unsigned int)slot;
    assert(out_u);
    assert(out_v);
    if( slot < 0 )
    {
        local_u = 0.5f;
        local_v = 0.5f;
    }
    if( local_u < 0.008f )
        local_u = 0.008f;
    else if( local_u > 0.992f )
        local_u = 0.992f;
    if( local_v < 0.008f )
        local_v = 0.008f;
    else if( local_v > 0.992f )
        local_v = 0.992f;
    *out_u = (float)(index & (GLES2_ATLAS_COLS - 1u)) * cell + local_u * cell;
    *out_v = (float)(index / GLES2_ATLAS_COLS) * cell + local_v * cell;
}

/* ---- per-frame stream sets --------------------------------------------------- */

/* Rotate every stream set onto this frame's buffer. */
static void
gles2_stream_sets_begin_frame(struct ToriRS_GLES2* renderer)
{
    struct GLES2StreamSet* sets[4];
    uint32_t set_index;
    renderer->frame_slot = (renderer->frame_slot + 1u) % GLES2_FRAMES_IN_FLIGHT;
    sets[0] = &renderer->index_stream;
    sets[1] = &renderer->dynamic_stream;
    sets[2] = &renderer->frame_stream;
    sets[3] = &renderer->ui_stream;
    for( set_index = 0u; set_index < 4u; set_index++ )
    {
        struct GLES2StreamSet* set = sets[set_index];
        set->head = 0u;
        if( !set->buffers[renderer->frame_slot] )
            glGenBuffers(1, &set->buffers[renderer->frame_slot]);
    }
    renderer->ui_vbo = renderer->ui_stream.buffers[renderer->frame_slot];
    renderer->frame_stream_vbo = renderer->frame_stream.buffers[renderer->frame_slot];
    renderer->ibo = renderer->index_stream.buffers[renderer->frame_slot];
    renderer->groups[TRSPK_VBO_GROUP_DYNAMIC].vbo_gpu =
        renderer->dynamic_stream.buffers[renderer->frame_slot];
}

/*
 * Append `bytes` to this frame's buffer of `set`, bound as `target`. The
 * buffer was last read GLES2_FRAMES_IN_FLIGHT frames ago, so the write
 * never lands on an outstanding draw. Returns the byte offset the payload
 * landed at.
 *
 * Growth reallocates the store with glBufferData(NULL) and the bytes already
 * in [0, offset) are GONE from the new store: ES 2.0 has no copy between
 * buffers and no read-back, so nothing here can carry them over. What GL
 * does guarantee is that draws already ISSUED against the old store keep
 * reading the old store (orphaning: a BufferData on a buffer with pending
 * reads leaves those reads their data). So growth is safe in exactly one of
 * two cases, and the caller says which:
 *
 *   offset == 0                 nothing appended this frame is lost;
 *   earlier_appends_drawn       every earlier append of this frame has had
 *                               its draw issued (the immediate 2D path draws
 *                               right after each append), so losing the
 *                               bytes loses nothing a draw still wants.
 *
 * Anything else -- an earlier append still waiting to be drawn when the
 * store is replaced -- would draw from a buffer whose prefix is undefined,
 * and is a contract violation here, not a case to handle.
 */
static uint32_t
gles2_stream_set_append(
    struct GLES2StreamSet* set,
    uint32_t slot,
    GLenum target,
    uint32_t initial_bytes,
    const void* data,
    uint32_t bytes,
    bool earlier_appends_drawn)
{
    uint32_t offset = set->head;
    assert(set->buffers[slot]);
    glBindBuffer(target, set->buffers[slot]);
    if( offset + bytes > set->capacities[slot] )
    {
        uint32_t capacity = set->capacities[slot] ? set->capacities[slot] : initial_bytes;
        assert(offset == 0u || earlier_appends_drawn);
        (void)earlier_appends_drawn; /* only the assert reads it; NDEBUG builds */
        while( capacity < offset + bytes )
            capacity *= 2u;
        glBufferData(target, (GLsizeiptr)capacity, NULL, GL_DYNAMIC_DRAW);
        set->capacities[slot] = capacity;
    }
    glBufferSubData(target, (GLintptr)offset, (GLsizeiptr)bytes, data);
    set->head = offset + ((bytes + 3u) & ~3u);
    return offset;
}

static void
gles2_stream_set_destroy(struct GLES2StreamSet* set)
{
    uint32_t slot;
    for( slot = 0u; slot < GLES2_FRAMES_IN_FLIGHT; slot++ )
        if( set->buffers[slot] )
            glDeleteBuffers(1, &set->buffers[slot]);
    memset(set, 0, sizeof(*set));
}

/* ---- retained groups --------------------------------------------------------- */

static bool
gles2_upload_group(struct ToriRS_GLES2* renderer, struct GLES2ModelGroup* group)
{
    uint32_t vertex_count;
    uint32_t first = 0u;
    uint32_t end;
    size_t byte_count;

    assert(renderer);
    assert(group);
    if( !group->vbo_cpu )
        return false;
    vertex_count = group->vbo_cpu->vertex_count;
    if( vertex_count == 0u )
    {
        trspk_vbo_clear_dirty(group->vbo_cpu);
        return true;
    }
    if( !group->reset_each_frame && !trspk_vbo_is_dirty(group->vbo_cpu) )
        return group->vbo_gpu != 0u;
    if( !group->reset_each_frame )
    {
        if( !group->vbo_gpu )
            glGenBuffers(1, &group->vbo_gpu);
        gles2_bind_array_buffer(renderer, group->vbo_gpu);
    }

    if( group->reset_each_frame )
    {
        /* Rebuilt wholesale every frame, so it goes into this frame's buffer
         * of the dynamic stream set (see GLES2_FRAMES_IN_FLIGHT). */
        uint32_t offset;
        byte_count = (size_t)vertex_count * sizeof(struct TRSPK_VertexGLES2);
        offset = gles2_stream_set_append(
            &renderer->dynamic_stream,
            renderer->frame_slot,
            GL_ARRAY_BUFFER,
            GLES2_DYNAMIC_STREAM_INIT_BYTES,
            group->vbo_cpu->vertices.as_gles2,
            (uint32_t)byte_count,
            false);
        renderer->bound_array_buffer = group->vbo_gpu;
        group->gpu_base_vertex = offset / (uint32_t)sizeof(struct TRSPK_VertexGLES2);
        group->gpu_capacity = renderer->dynamic_stream.capacities[renderer->frame_slot] /
            (uint32_t)sizeof(struct TRSPK_VertexGLES2);
        trspk_vbo_clear_dirty(group->vbo_cpu);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DYNAMIC_VBO_UPLOAD_BYTES, (int64_t)byte_count);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DYNAMIC_VBO_UPLOADS, 1);
        return true;
    }

    if( vertex_count > group->gpu_capacity )
    {
        uint32_t capacity = group->gpu_capacity ? group->gpu_capacity : GLES2_GPU_BUFFER_INIT;
        while( capacity < vertex_count )
            capacity *= 2u;
        glBufferData(
            GL_ARRAY_BUFFER,
            (GLsizeiptr)((size_t)capacity * sizeof(struct TRSPK_VertexGLES2)),
            NULL,
            GL_STATIC_DRAW);
        group->gpu_capacity = capacity;
        /* Nothing is in a buffer that did not exist a moment ago. */
        trspk_vbo_set_dirty(group->vbo_cpu);
    }
    /* Upload only what changed. The static group is one buffer holding every
     * retained model, and its flag is set by any one of them re-baking; the
     * bake records which vertices it wrote, and this sends that span. */
    end = vertex_count;
    if( group->vbo_cpu->dirty_end > group->vbo_cpu->dirty_first )
    {
        first = group->vbo_cpu->dirty_first;
        end = group->vbo_cpu->dirty_end;
        if( end > vertex_count )
            end = vertex_count;
        if( first > end )
            first = end;
    }
    byte_count = (size_t)(end - first) * sizeof(struct TRSPK_VertexGLES2);
    if( byte_count > 0u )
    {
        glBufferSubData(
            GL_ARRAY_BUFFER,
            (GLintptr)((size_t)first * sizeof(struct TRSPK_VertexGLES2)),
            (GLsizeiptr)byte_count,
            &group->vbo_cpu->vertices.as_gles2[first]);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_STATIC_VBO_UPLOAD_BYTES, (int64_t)byte_count);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_STATIC_VBO_UPLOADS, 1);
    }
    trspk_vbo_clear_dirty(group->vbo_cpu);
    return true;
}

static void
gles2_reset_group(struct GLES2ModelGroup* group)
{
    assert(group);
    if( group->arena )
        trspk_modelarena_clear(group->arena);
}

/* ---- static batches (Batch16 pages) ------------------------------------------ */

static bool
gles2_upload_dirty_static_batches(struct ToriRS_GLES2* renderer);

static void
gles2_mark_active_static_batches_dirty(struct ToriRS_GLES2* renderer)
{
    uint32_t batch_slot;
    assert(renderer);
    for( batch_slot = 0u; batch_slot < renderer->static_batch_count; batch_slot++ )
    {
        struct GLES2StaticBatch* batch = &renderer->static_batches[batch_slot];
        uint32_t chunk_count;
        uint32_t chunk_index;
        if( !batch->active || !batch->cpu )
            continue;
        chunk_count = trspk_batch16_chunk_count(batch->cpu);
        for( chunk_index = 0u; chunk_index < chunk_count; chunk_index++ )
        {
            struct TRSPK_Batch16Chunk* chunk = trspk_batch16_get_chunk(batch->cpu, chunk_index);
            if( chunk && chunk->vbo )
                trspk_vbo_set_dirty(chunk->vbo);
        }
    }
    renderer->static_batch_upload_pending = true;
}

static void
gles2_grow_static_batches(struct ToriRS_GLES2* renderer, uint32_t needed)
{
    struct GLES2StaticBatch* grown;
    uint32_t capacity;
    if( needed <= renderer->static_batch_capacity )
        return;
    capacity = renderer->static_batch_capacity ? renderer->static_batch_capacity : 8u;
    while( capacity < needed )
        capacity *= 2u;
    grown = (struct GLES2StaticBatch*)realloc(
        renderer->static_batches, (size_t)capacity * sizeof(*grown));
    assert(grown);
    memset(
        grown + renderer->static_batch_capacity,
        0,
        (size_t)(capacity - renderer->static_batch_capacity) * sizeof(*grown));
    renderer->static_batches = grown;
    renderer->static_batch_capacity = capacity;
}

static int
gles2_static_batch_slot(struct ToriRS_GLES2* renderer, int batch_id, bool create)
{
    uint32_t slot;
    uint32_t reusable = UINT32_MAX;
    assert(renderer);
    if( batch_id < 0 )
        return -1;
    for( slot = 0u; slot < renderer->static_batch_count; slot++ )
    {
        if( renderer->static_batches[slot].batch_id == batch_id )
            return (int)slot;
        if( reusable == UINT32_MAX && !renderer->static_batches[slot].active &&
            !renderer->static_batches[slot].building &&
            trspk_batch16_entry_count(renderer->static_batches[slot].cpu) == 0u )
            reusable = slot;
    }
    if( !create )
        return -1;
    if( reusable != UINT32_MAX )
    {
        renderer->static_batches[reusable].batch_id = batch_id;
        return (int)reusable;
    }
    gles2_grow_static_batches(renderer, renderer->static_batch_count + 1u);
    slot = renderer->static_batch_count++;
    renderer->static_batches[slot].batch_id = batch_id;
    renderer->static_batches[slot].cpu = trspk_batch16_create(TRSPK_VERTEX_FORMAT_GLES2);
    assert(renderer->static_batches[slot].cpu);
    return (int)slot;
}

static void
gles2_rebuild_batch_pose_table(struct ToriRS_GLES2* renderer)
{
    uint32_t batch_slot;
    assert(renderer);
    trspk_pose_table_clear(&renderer->batch_poses);
    for( batch_slot = 0u; batch_slot < renderer->static_batch_count; batch_slot++ )
    {
        const struct GLES2StaticBatch* batch = &renderer->static_batches[batch_slot];
        uint32_t entry_count;
        uint32_t entry_index;
        if( !batch->active || !batch->cpu )
            continue;
        entry_count = trspk_batch16_entry_count(batch->cpu);
        for( entry_index = 0u; entry_index < entry_count; entry_index++ )
        {
            const struct TRSPK_Batch16Entry* entry =
                trspk_batch16_get_entry(batch->cpu, entry_index);
            uint32_t page_id;
            if( !entry || entry->element_id < 0 || entry->chunk_index >= batch->page_id_capacity ||
                entry_index > GLES2_BATCH_POSE_ENTRY_MASK ||
                batch_slot > GLES2_BATCH_POSE_SLOT_MASK )
                continue;
            page_id = batch->page_ids[entry->chunk_index];
            if( page_id >= renderer->static_page_count || !renderer->static_pages[page_id].valid )
                continue;
            trspk_pose_table_set(
                &renderer->batch_poses,
                entry->element_id,
                entry->anim_index,
                entry->pose_id,
                GLES2_BATCH_POSE_FLAG | (batch_slot << GLES2_BATCH_POSE_SLOT_SHIFT) | entry_index);
        }
    }
}

static bool
gles2_grow_static_pages(struct ToriRS_GLES2* renderer, uint32_t needed)
{
    struct GLES2StaticPageRef* grown;
    uint32_t capacity;
    if( needed <= renderer->static_page_capacity )
        return true;
    if( needed > GLES2_BATCH_PAGE_LIMIT )
        return false;
    capacity = renderer->static_page_capacity ? renderer->static_page_capacity : 32u;
    while( capacity < needed )
    {
        if( capacity >= GLES2_BATCH_PAGE_LIMIT / 2u )
        {
            capacity = GLES2_BATCH_PAGE_LIMIT;
            break;
        }
        capacity *= 2u;
    }
    grown = (struct GLES2StaticPageRef*)realloc(
        renderer->static_pages, (size_t)capacity * sizeof(*grown));
    assert(grown);
    memset(
        grown + renderer->static_page_capacity,
        0,
        (size_t)(capacity - renderer->static_page_capacity) * sizeof(*grown));
    renderer->static_pages = grown;
    renderer->static_page_capacity = capacity;
    return true;
}

static void
gles2_static_batch_ensure_chunk_storage(struct GLES2StaticBatch* batch, uint32_t chunk_count)
{
    uint32_t* grown;
    uint32_t old_capacity;
    uint32_t capacity;
    uint32_t chunk;
    assert(batch);
    if( chunk_count <= batch->page_id_capacity )
        return;
    old_capacity = batch->page_id_capacity;
    capacity = batch->page_id_capacity ? batch->page_id_capacity : 4u;
    while( capacity < chunk_count )
        capacity *= 2u;
    grown = (uint32_t*)realloc(batch->page_ids, (size_t)capacity * sizeof(*grown));
    assert(grown);
    batch->page_ids = grown;
    for( chunk = old_capacity; chunk < capacity; chunk++ )
        batch->page_ids[chunk] = UINT32_MAX;
    batch->page_id_capacity = capacity;
}

static bool
gles2_static_batch_assign_page(
    struct ToriRS_GLES2* renderer,
    uint32_t batch_slot,
    uint32_t chunk_index)
{
    struct GLES2StaticBatch* batch = &renderer->static_batches[batch_slot];
    const struct TRSPK_Batch16Chunk* chunk;
    struct GLES2StaticPageRef* page;
    uint32_t page_id;
    uint32_t needed;
    assert(chunk_index < batch->page_id_capacity);
    chunk = trspk_batch16_get_chunk(batch->cpu, chunk_index);
    needed = chunk ? chunk->vertex_count : 0u;
    page_id = batch->page_ids[chunk_index];
    if( page_id == UINT32_MAX )
    {
        if( renderer->static_page_count >= GLES2_BATCH_PAGE_LIMIT ||
            !gles2_grow_static_pages(renderer, renderer->static_page_count + 1u) )
            return false;
        page_id = renderer->static_page_count++;
        batch->page_ids[chunk_index] = page_id;
        renderer->static_pages[page_id].gpu_offset = 0u;
        renderer->static_pages[page_id].gpu_capacity = 0u;
    }
    page = &renderer->static_pages[page_id];
    page->batch_slot = batch_slot;
    page->chunk_index = chunk_index;
    /* The painter's placement reads the bake through this (see the field). */
    page->cpu_vbo = chunk ? chunk->vbo : NULL;
    page->valid = page->cpu_vbo != NULL;
    /* A range it outgrew is abandoned, not extended: the bump allocator only
     * ever hands out the tail, and the commit compacts when the tail runs
     * out (gles2_compact_static_pages). */
    if( needed > page->gpu_capacity )
    {
        page->gpu_offset = renderer->static_batch_gpu_vertex_used;
        page->gpu_capacity = needed;
        renderer->static_batch_gpu_vertex_used += needed;
    }
    return true;
}

/* Re-pack every valid page densely, in page order, and re-send them all.
 * Returns the packed high-water mark. */
static uint32_t
gles2_compact_static_pages(struct ToriRS_GLES2* renderer)
{
    uint32_t page_id;
    uint32_t used = 0u;
    assert(renderer);
    for( page_id = 0u; page_id < renderer->static_page_count; page_id++ )
    {
        struct GLES2StaticPageRef* page = &renderer->static_pages[page_id];
        const struct GLES2StaticBatch* batch;
        const struct TRSPK_Batch16Chunk* chunk;
        if( !page->valid || page->batch_slot >= renderer->static_batch_count )
        {
            page->gpu_capacity = 0u;
            continue;
        }
        batch = &renderer->static_batches[page->batch_slot];
        chunk = batch->cpu ? trspk_batch16_get_chunk(batch->cpu, page->chunk_index) : NULL;
        page->gpu_capacity = chunk ? chunk->vertex_count : 0u;
        page->gpu_offset = used;
        used += page->gpu_capacity;
    }
    renderer->static_batch_gpu_vertex_used = used;
    gles2_mark_active_static_batches_dirty(renderer);
    return used;
}

static void
gles2_invalidate_batch_pages(struct ToriRS_GLES2* renderer, const struct GLES2StaticBatch* batch)
{
    uint32_t chunk;
    for( chunk = 0u; chunk < batch->page_id_capacity; chunk++ )
    {
        uint32_t page_id = batch->page_ids[chunk];
        if( page_id != UINT32_MAX && page_id < renderer->static_page_count )
        {
            renderer->static_pages[page_id].valid = false;
            renderer->static_pages[page_id].cpu_vbo = NULL;
        }
    }
}

/* The page buffer holds `capacity` vertices; growing it is a fresh
 * allocation and every page has to be re-sent from its CPU chunk. */
static bool
gles2_ensure_static_batch_vbo(
    struct ToriRS_GLES2* renderer,
    uint32_t required_vertices,
    bool* out_recreated)
{
    uint32_t capacity;
    uint64_t byte_capacity;
    assert(renderer);
    assert(out_recreated);
    *out_recreated = false;
    if( required_vertices == 0u )
        return true;
    if( renderer->static_batch_vbo &&
        renderer->static_batch_gpu_vertex_capacity >= required_vertices )
        return true;
    if( !renderer->gl_context )
        return true;
    capacity = renderer->static_batch_gpu_vertex_capacity
        ? renderer->static_batch_gpu_vertex_capacity
        : GLES2_STATIC_BATCH_VBO_INIT_VERTICES;
    while( capacity < required_vertices )
    {
        if( capacity > UINT32_MAX / 2u )
        {
            capacity = required_vertices;
            break;
        }
        capacity *= 2u;
    }
    byte_capacity = (uint64_t)capacity * sizeof(struct TRSPK_VertexGLES2);
    if( byte_capacity > (uint64_t)INT32_MAX )
        return false;
    if( !renderer->static_batch_vbo )
        glGenBuffers(1, &renderer->static_batch_vbo);
    gles2_bind_array_buffer(renderer, renderer->static_batch_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)byte_capacity, NULL, GL_STATIC_DRAW);
    if( !gles2_check_error("static batch page buffer") )
        return false;
    renderer->static_batch_gpu_vertex_capacity = capacity;
    *out_recreated = true;
    return true;
}

static bool
gles2_upload_static_batch_chunk(
    struct ToriRS_GLES2* renderer,
    struct GLES2StaticBatch* batch,
    uint32_t chunk_index)
{
    struct TRSPK_Batch16Chunk* chunk;
    uint32_t page_id;
    uint32_t first;
    uint32_t end;
    size_t byte_count;

    assert(renderer);
    assert(batch);
    assert(batch->cpu);
    if( chunk_index >= batch->page_id_capacity )
        return false;
    chunk = trspk_batch16_get_chunk(batch->cpu, chunk_index);
    if( !chunk || !chunk->vbo )
        return false;
    if( chunk->vertex_count == 0u )
    {
        trspk_vbo_clear_dirty(chunk->vbo);
        return true;
    }
    if( !renderer->gl_context )
        return true;
    if( !renderer->static_batch_vbo || !trspk_vbo_is_dirty(chunk->vbo) )
        return renderer->static_batch_vbo != 0u;
    page_id = batch->page_ids[chunk_index];
    if( page_id == UINT32_MAX || page_id >= renderer->static_page_count ||
        !renderer->static_pages[page_id].valid ||
        renderer->static_pages[page_id].gpu_offset + chunk->vertex_count >
            renderer->static_batch_gpu_vertex_capacity )
        return false;
    /* The chunk's own dirty span, when it has one; a fresh page sends all. */
    first = 0u;
    end = chunk->vertex_count;
    if( chunk->vbo->dirty_end > chunk->vbo->dirty_first )
    {
        first = chunk->vbo->dirty_first;
        end = chunk->vbo->dirty_end < chunk->vertex_count ? chunk->vbo->dirty_end
                                                          : chunk->vertex_count;
        if( first > end )
            first = end;
    }
    byte_count = (size_t)(end - first) * sizeof(struct TRSPK_VertexGLES2);
    if( byte_count > 0u )
    {
        gles2_bind_array_buffer(renderer, renderer->static_batch_vbo);
        glBufferSubData(
            GL_ARRAY_BUFFER,
            (GLintptr)(((uint64_t)renderer->static_pages[page_id].gpu_offset + first) *
                       sizeof(struct TRSPK_VertexGLES2)),
            (GLsizeiptr)byte_count,
            &chunk->vbo->vertices.as_gles2[first]);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_STATIC_VBO_UPLOAD_BYTES, (int64_t)byte_count);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_STATIC_VBO_UPLOADS, 1);
    }
    trspk_vbo_clear_dirty(chunk->vbo);
    return true;
}

static bool
gles2_upload_dirty_static_batches(struct ToriRS_GLES2* renderer)
{
    uint32_t batch_slot;
    bool recreated = false;
    assert(renderer);
    if( !renderer->static_batch_upload_pending )
        return true;
    if( !gles2_ensure_static_batch_vbo(
            renderer, renderer->static_batch_gpu_vertex_used, &recreated) )
        return false;
    if( recreated )
        gles2_mark_active_static_batches_dirty(renderer);
    for( batch_slot = 0u; batch_slot < renderer->static_batch_count; batch_slot++ )
    {
        struct GLES2StaticBatch* batch = &renderer->static_batches[batch_slot];
        uint32_t chunk_count;
        uint32_t chunk;
        if( !batch->active || !batch->cpu )
            continue;
        chunk_count = trspk_batch16_chunk_count(batch->cpu);
        for( chunk = 0u; chunk < chunk_count; chunk++ )
            if( !gles2_upload_static_batch_chunk(renderer, batch, chunk) )
                return false;
    }
    renderer->static_batch_upload_pending = false;
    return true;
}

static bool
gles2_static_batch_commit(struct ToriRS_GLES2* renderer, uint32_t batch_slot)
{
    struct GLES2StaticBatch* batch;
    uint32_t chunk_count;
    uint32_t chunk;
    bool recreated = false;

    assert(renderer);
    assert(batch_slot < renderer->static_batch_count);
    batch = &renderer->static_batches[batch_slot];
    assert(batch->cpu);
    batch->active = false;
    chunk_count = trspk_batch16_chunk_count(batch->cpu);
    gles2_static_batch_ensure_chunk_storage(batch, chunk_count);
    gles2_painter_batch_reset(renderer, batch, trspk_batch16_entry_count(batch->cpu));
    gles2_invalidate_batch_pages(renderer, batch);
    for( chunk = 0u; chunk < chunk_count; chunk++ )
        if( !gles2_static_batch_assign_page(renderer, batch_slot, chunk) )
            goto fail;
    /* The tail ran past the buffer: pack the holes the rebuilt chunks left
     * before buying a bigger buffer, since either way everything re-sends. */
    if( renderer->static_batch_vbo &&
        renderer->static_batch_gpu_vertex_used > renderer->static_batch_gpu_vertex_capacity )
        (void)gles2_compact_static_pages(renderer);
    if( !gles2_ensure_static_batch_vbo(
            renderer, renderer->static_batch_gpu_vertex_used, &recreated) )
        goto fail;
    if( recreated || renderer->static_batch_upload_pending )
    {
        gles2_mark_active_static_batches_dirty(renderer);
        if( !gles2_upload_dirty_static_batches(renderer) )
            goto fail;
    }
    for( chunk = 0u; chunk < chunk_count; chunk++ )
        if( !gles2_upload_static_batch_chunk(renderer, batch, chunk) )
            goto fail;
    batch->active = true;
    gles2_rebuild_batch_pose_table(renderer);
    return true;

fail:
    gles2_invalidate_batch_pages(renderer, batch);
    gles2_rebuild_batch_pose_table(renderer);
    return false;
}

static bool
gles2_resolve_static_page(
    struct ToriRS_GLES2* renderer,
    uint32_t page_id,
    struct TRSPK_Batch16Chunk** out_chunk)
{
    const struct GLES2StaticPageRef* ref;
    struct GLES2StaticBatch* batch;
    struct TRSPK_Batch16Chunk* chunk;
    assert(renderer);
    assert(out_chunk);
    if( page_id >= renderer->static_page_count || !renderer->static_pages[page_id].valid )
        return false;
    ref = &renderer->static_pages[page_id];
    if( ref->batch_slot >= renderer->static_batch_count )
        return false;
    batch = &renderer->static_batches[ref->batch_slot];
    if( !batch->active || !batch->cpu ||
        ref->chunk_index >= trspk_batch16_chunk_count(batch->cpu) )
        return false;
    chunk = trspk_batch16_get_chunk(batch->cpu, ref->chunk_index);
    if( !chunk )
        return false;
    *out_chunk = chunk;
    return true;
}

bool
gles2_binding_cpu_source(
    struct ToriRS_GLES2* renderer,
    uint32_t binding,
    uint32_t page_id,
    const struct TRSPK_VBO** out_vbo,
    const struct TRSPK_Triangles** out_triangles)
{
    assert(renderer);
    assert(out_vbo);
    assert(out_triangles);
    if( binding < TRSPK_VBO_GROUP_COUNT )
    {
        *out_vbo = renderer->groups[binding].vbo_cpu;
        *out_triangles = &renderer->groups[binding].triangles;
        return *out_vbo != NULL;
    }
    if( binding == GLES2_STATIC_PAGE_BINDING )
    {
        struct TRSPK_Batch16Chunk* chunk = NULL;
        if( !gles2_resolve_static_page(renderer, page_id, &chunk) )
            return false;
        *out_vbo = chunk->vbo;
        *out_triangles = &chunk->triangles;
        return *out_vbo != NULL;
    }
    if( binding == GLES2_FRAME_STREAM_BINDING )
    {
        *out_vbo = renderer->frame_stream_cpu;
        *out_triangles = &renderer->frame_stream_triangles;
        return *out_vbo != NULL;
    }
    return false;
}

/* ---- pose tables and the static arena ------------------------------------------ */

static void
gles2_rebuild_static_pose_table(struct ToriRS_GLES2* renderer)
{
    struct TRSPK_ModelArena* arena;
    uint32_t slot_index;
    assert(renderer);
    arena = renderer->groups[TRSPK_VBO_GROUP_STATIC].arena;
    trspk_pose_table_clear(&renderer->poses);
    if( !arena )
        return;
    for( slot_index = 0u; slot_index < arena->slot_count; slot_index++ )
    {
        const struct TRSPK_ModelSlot* slot = &arena->slots[slot_index];
        if( !trspk_modelslot_is_alive(slot) || slot->element_id < 0 || slot->pose_id < 0 )
            continue;
        trspk_pose_table_set(
            &renderer->poses,
            slot->element_id,
            slot->pose_id % TRSPK_POSE_TRACK_COUNT,
            slot->pose_id / TRSPK_POSE_TRACK_COUNT,
            slot->vertex_base);
    }
}

/* Reclaim unloaded ranges. Without it the arena only grows, and since the
 * draw binding IS the page, growth means more pages and more draws. */
static void
gles2_compact_static_group(struct ToriRS_GLES2* renderer)
{
    struct TRSPK_ModelArena* arena;
    struct TRSPK_ModelArenaGCResult result;
    assert(renderer);
    arena = renderer->groups[TRSPK_VBO_GROUP_STATIC].arena;
    if( !arena )
        return;
    result = trspk_modelarena_gc(arena);
    if( result.did_compact )
        gles2_rebuild_static_pose_table(renderer);
}

static bool
gles2_pose_element_is_retained(const struct ToriRS_GLES2* renderer, int element_id)
{
    uint32_t element_index;
    int track;
    assert(renderer);
    if( element_id < 0 || !renderer->poses.elements )
        return false;
    element_index = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id);
    if( element_index >= renderer->poses.element_count )
        return false;
    for( track = 0; track < TRSPK_POSE_TRACK_COUNT; track++ )
        if( renderer->poses.elements[element_index].tracks[track].pose_count > 0u )
            return true;
    return false;
}

static bool
gles2_pose_track_is_retained(
    const struct ToriRS_GLES2* renderer,
    int element_id,
    int anim_index)
{
    uint32_t element_index;
    assert(renderer);
    if( element_id < 0 || anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT ||
        !renderer->poses.elements )
        return false;
    element_index = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id);
    return element_index < renderer->poses.element_count &&
        renderer->poses.elements[element_index].tracks[anim_index].pose_count > 0u;
}

/* ---- baking ---------------------------------------------------------------------- */

/*
 * Bake one model pose into `vbo` at `vertex_base`, three vertices per face.
 *
 * Every texture decision is made here, once: the atlas slot (reserved even
 * when the texture is still loading, so the vertex already names the tile the
 * upload will fill), the tile bytes, the scroll bytes, and the colour in RGBA
 * byte order. The vertex keeps its LOCAL uv; the fragment shader wraps and
 * clamps it into the tile.
 */
static bool
gles2_bake_pose_vertices(
    struct ToriRS_GLES2* renderer,
    struct TRSPK_VBO* vbo,
    struct TRSPK_Triangles* triangles,
    uint32_t vertex_base,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position,
    const int* face_order,
    int order_count)
{
    struct TRSPK_WorldPlacement placement;
    int face_count;
    uint32_t order_index;
    uint32_t written_count;

    assert(renderer);
    assert(renderer->scene);
    assert(vbo);
    assert(triangles);
    face_count = trspk_toridraw_face_count(model_handle);
    if( face_count <= 0 )
        return false;
    /* With a face order the pose is written in THAT order -- the painter
     * path's sorted actors -- and only the faces the order names. */
    written_count = face_order ? (uint32_t)(order_count > 0 ? order_count : 0) : (uint32_t)face_count;
    trspk_toridraw_placement_init(&placement, world_position);

    for( order_index = 0u; order_index < written_count; order_index++ )
    {
        struct TRSPK_ToriDrawBakeFaceVerts face;
        uint32_t face_index = face_order ? (uint32_t)face_order[order_index] : order_index;
        uint32_t vertex = vertex_base + order_index * 3u;
        uint8_t tile_col = 0u;
        uint8_t tile_row = 0u;
        uint8_t anim_u = TRSPK_VERTEX_GLES2_ANIM_STILL;
        uint8_t anim_v = TRSPK_VERTEX_GLES2_ANIM_STILL;
        float ua;
        float va;
        float ub;
        float vb;
        float uc;
        float vc;
        int config = GLES2_TRIANGLE_UNTEXTURED;

        if( face_index >= (uint32_t)face_count ||
            !trspk_toridraw_bake_face_handle(
                model_handle,
                face_index,
                &placement,
                renderer->scene,
                true,
                TRSPK_BAKE_COLOR_ARGB,
                &face) )
        {
            /* A skipped face still owns its triplet in an ordered bake: leave
             * it fully transparent so the alpha test drops it. */
            if( face_order )
            {
                trspk_triangles_set(
                    triangles, trspk_triangles_index_from_vertex(vertex), GLES2_TRIANGLE_UNTEXTURED);
                trspk_vbo_write_vertex_gles2(vbo, vertex, 0.0f, 0.0f, 0.0f, 0u, 0.5f, 0.5f, 0u, 0u,
                    TRSPK_VERTEX_GLES2_ANIM_STILL, TRSPK_VERTEX_GLES2_ANIM_STILL);
                trspk_vbo_write_vertex_gles2(vbo, vertex + 1u, 0.0f, 0.0f, 0.0f, 0u, 0.5f, 0.5f, 0u,
                    0u, TRSPK_VERTEX_GLES2_ANIM_STILL, TRSPK_VERTEX_GLES2_ANIM_STILL);
                trspk_vbo_write_vertex_gles2(vbo, vertex + 2u, 0.0f, 0.0f, 0.0f, 0u, 0.5f, 0.5f, 0u,
                    0u, TRSPK_VERTEX_GLES2_ANIM_STILL, TRSPK_VERTEX_GLES2_ANIM_STILL);
            }
            continue;
        }

        if( face.tex_id >= 0 )
        {
            int slot = gles2_ensure_texture(renderer, face.tex_id);
            config = face.tex_id;
            if( slot >= 0 )
            {
                tile_col = (uint8_t)((uint32_t)slot & (GLES2_ATLAS_COLS - 1u));
                tile_row = (uint8_t)((uint32_t)slot / GLES2_ATLAS_COLS);
                gles2_texture_anim_bytes(
                    gles2_scene_texture(renderer, face.tex_id), &anim_u, &anim_v);
            }
            else
            {
                /* Slot zero is the opaque white tile for genuinely untextured
                 * faces. A textured face the atlas could not take must not use
                 * it as a visible fallback. */
                face.argb_a &= 0x00FFFFFFu;
                face.argb_b &= 0x00FFFFFFu;
                face.argb_c &= 0x00FFFFFFu;
            }
            ua = face.uv.u1;
            va = face.uv.v1;
            ub = face.uv.u2;
            vb = face.uv.v2;
            uc = face.uv.u3;
            vc = face.uv.v3;
        }
        else
        {
            /* The white tile's centre: the colour is the whole answer. */
            ua = ub = uc = 0.5f;
            va = vb = vc = 0.5f;
        }

        trspk_triangles_set(triangles, trspk_triangles_index_from_vertex(vertex), config);
        trspk_vbo_write_vertex_gles2(
            vbo, vertex, face.wx_a, face.wy_a, face.wz_a,
            gles2_argb_to_rgba_bytes(face.argb_a), ua, va, tile_col, tile_row, anim_u, anim_v);
        trspk_vbo_write_vertex_gles2(
            vbo, vertex + 1u, face.wx_b, face.wy_b, face.wz_b,
            gles2_argb_to_rgba_bytes(face.argb_b), ub, vb, tile_col, tile_row, anim_u, anim_v);
        trspk_vbo_write_vertex_gles2(
            vbo, vertex + 2u, face.wx_c, face.wy_c, face.wz_c,
            gles2_argb_to_rgba_bytes(face.argb_c), uc, vc, tile_col, tile_row, anim_u, anim_v);
    }
    /* Once for the model rather than three times per face -- and as a RANGE,
     * because this model is the only part of a shared retained buffer that
     * changed. */
    trspk_vbo_mark_dirty_range(vbo, vertex_base, written_count * 3u);
    return true;
}

static uint32_t
gles2_bake_into_arena(
    struct ToriRS_GLES2* renderer,
    struct GLES2ModelGroup* group,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position,
    bool update_pose_table)
{
    int face_count;
    uint32_t vertex_count;
    int arena_element_id;
    int arena_pose_id;
    uint32_t slot_index;
    const struct TRSPK_ModelSlot* model_slot;

    assert(renderer);
    assert(renderer->scene);
    assert(group);
    assert(group->arena);
    assert(group->vbo_cpu);
    face_count = trspk_toridraw_face_count(model_handle);
    if( face_count <= 0 || (uint32_t)face_count > UINT32_MAX / 3u )
        return UINT32_MAX;
    vertex_count = (uint32_t)face_count * 3u;
    if( vertex_count > TRSPK_BATCH16_MAX_VERTICES )
    {
        TORIRS_ERR("GLES2: model has %lu vertices and cannot fit a 16-bit page\n",
            (unsigned long)vertex_count);
        return UINT32_MAX;
    }
    anim_index = gles2_clampi(anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1);
    if( pose_id < 0 )
        pose_id = 0;
    if( pose_id > (INT_MAX - anim_index) / TRSPK_POSE_TRACK_COUNT )
        return UINT32_MAX;
    arena_element_id = element_id >= 0 ? element_id : 0;
    arena_pose_id = pose_id * TRSPK_POSE_TRACK_COUNT + anim_index;
    if( update_pose_table && element_id < 0 )
        update_pose_table = false;
    if( update_pose_table )
    {
        uint32_t old_slot = trspk_modelarena_find(group->arena, arena_element_id, arena_pose_id);
        if( old_slot != TRSPK_MODELSLOT_NULL_IDX )
        {
            trspk_modelarena_unload(group->arena, old_slot);
            if( group == &renderer->groups[TRSPK_VBO_GROUP_STATIC] )
                gles2_compact_static_group(renderer);
        }
    }
    slot_index = trspk_modelarena_load(group->arena, arena_element_id, arena_pose_id, vertex_count);
    model_slot = trspk_modelarena_get(group->arena, slot_index);
    if( !model_slot ||
        !gles2_bake_pose_vertices(
            renderer,
            group->vbo_cpu,
            &group->triangles,
            model_slot->vertex_base,
            model_handle,
            world_position,
            NULL,
            0) )
        return UINT32_MAX;
    if( update_pose_table )
    {
        trspk_pose_table_set(
            &renderer->poses, element_id, anim_index, pose_id, model_slot->vertex_base);
        gles2_zbuffer_pose_baked(renderer, element_id, anim_index, pose_id, model_handle);
    }
    return model_slot->vertex_base;
}

static void
gles2_model_unload(struct ToriRS_GLES2* renderer, int element_id)
{
    assert(renderer);
    /* Individual unloads own only the arena. Batch geometry and its pose map
     * remain immutable until the matching batch rebuild/clear. */
    if( element_id < 0 || !renderer->groups[TRSPK_VBO_GROUP_STATIC].arena ||
        !gles2_pose_element_is_retained(renderer, element_id) )
        return;
    trspk_modelarena_unload_element(renderer->groups[TRSPK_VBO_GROUP_STATIC].arena, element_id);
    trspk_pose_table_remove_element(&renderer->poses, element_id);
    gles2_zbuffer_element_dropped(renderer, element_id);
    gles2_compact_static_group(renderer);
}

static void
gles2_animation_track_unload(struct ToriRS_GLES2* renderer, int element_id, int anim_index)
{
    struct TRSPK_ModelArena* arena;
    uint32_t slot_index;
    assert(renderer);
    if( element_id < 0 || anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT )
        return;
    arena = renderer->groups[TRSPK_VBO_GROUP_STATIC].arena;
    if( !arena || !gles2_pose_track_is_retained(renderer, element_id, anim_index) )
        return;
    for( slot_index = 0u; slot_index < arena->slot_count; slot_index++ )
    {
        const struct TRSPK_ModelSlot* slot = &arena->slots[slot_index];
        if( trspk_modelslot_is_alive(slot) && slot->element_id == element_id &&
            slot->pose_id % TRSPK_POSE_TRACK_COUNT == anim_index )
            trspk_modelarena_unload(arena, slot_index);
    }
    trspk_pose_table_remove_track(&renderer->poses, element_id, anim_index);
    gles2_zbuffer_track_dropped(renderer, element_id, anim_index);
    gles2_compact_static_group(renderer);
}

static void
gles2_model_load(struct ToriRS_GLES2* renderer, const struct ToriRS_RenderCommand_ModelLoad* command)
{
    assert(renderer);
    assert(command);
    if( command->element_id < 0 || command->model.kind == TORIDRAWMK_NONE )
        return;
    /* A model replacement invalidates every pose from the old geometry. */
    gles2_model_unload(renderer, command->element_id);
    (void)gles2_bake_into_arena(
        renderer,
        &renderer->groups[TRSPK_VBO_GROUP_STATIC],
        command->element_id,
        0,
        0,
        command->model,
        &command->world_position,
        true);
}

static void
gles2_animation_load(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_AnimLoad* command)
{
    struct ToriDraw_Animation* animation;
    struct ToriDraw_SkeletalAnim* skeletal;
    struct ToriDraw_Model* source;
    int anim_index;
    int frame;

    assert(renderer);
    assert(command);
    if( command->element_id < 0 || !command->animation || command->animation->frame_count <= 0 ||
        !ToriDraw_ModelKindIsFull(command->model.kind) || !command->model.u.model.model )
        return;
    animation = command->animation;
    skeletal = animation->skeletal;
    if( !skeletal && (!animation->base || !animation->frames) )
        return;
    anim_index = gles2_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1);
    /* Pose keys do not carry a sequence id. Clear the old track so frame zero
     * cannot keep resolving to MODEL_LOAD's rest pose and a shorter
     * replacement cannot serve stale tail frames. */
    gles2_animation_track_unload(renderer, command->element_id, anim_index);
    source = command->model.u.model.model;
    for( frame = 0; frame < animation->frame_count; frame++ )
    {
        struct ToriDraw_Model* baked = ToriDraw_ModelCopy(source);
        struct ToriDraw_ModelHandle handle;
        bool posed = false;
        assert(baked);
        /* ModelCopy copies the current vertices, not the captured rest arrays;
         * seed the copy from the source's rest pose when one exists. */
        if( source->original_vertices_x && source->original_vertices_y &&
            source->original_vertices_z && baked->vertex_count == source->vertex_count )
        {
            size_t vertex_bytes = (size_t)baked->vertex_count * sizeof(*baked->vertices_x);
            memcpy(baked->vertices_x, source->original_vertices_x, vertex_bytes);
            memcpy(baked->vertices_y, source->original_vertices_y, vertex_bytes);
            memcpy(baked->vertices_z, source->original_vertices_z, vertex_bytes);
        }
        if( baked->face_alphas && source->original_face_alphas &&
            baked->face_count == source->face_count )
            memcpy(
                baked->face_alphas,
                source->original_face_alphas,
                (size_t)baked->face_count * sizeof(*baked->face_alphas));
        ToriDraw_ModelCaptureOriginalVertices(baked);
        if( skeletal )
        {
            int skeletal_frame = frame < skeletal->frame_count ? frame : 0;
            if( skeletal->frame_count > 0 && skeletal->matrices && baked->animaya_vertex_count > 0 &&
                baked->animaya_group_counts && baked->animaya_groups && baked->animaya_scales )
            {
                ToriDraw_ModelAnimateSkeletal(baked, skeletal, skeletal_frame);
                posed = true;
            }
        }
        else if( animation->frames[frame].length > 0 )
        {
            ToriDraw_ModelAnimateFrame(baked, animation->base, &animation->frames[frame]);
            posed = true;
        }
        /* Every pose that DID run has re-applied the model's post-animation
         * resize; the rest pose still has to be baked at render scale. */
        if( !posed )
            ToriDraw_ModelApplyPostTransforms(baked);
        memset(&handle, 0, sizeof(handle));
        handle.kind = TORIDRAWMK_MODEL;
        handle.u.model.model = baked;
        (void)gles2_bake_into_arena(
            renderer,
            &renderer->groups[TRSPK_VBO_GROUP_STATIC],
            command->element_id,
            anim_index,
            frame,
            handle,
            &command->world_position,
            true);
        ToriDraw_ModelFree(baked);
    }
}

bool
gles2_reserve_model_indices(struct ToriRS_GLES2* renderer, uint32_t needed)
{
    uint16_t* grown;
    uint32_t capacity;
    assert(renderer);
    if( needed <= renderer->model_index_capacity )
        return true;
    capacity = renderer->model_index_capacity ? renderer->model_index_capacity : 256u;
    while( capacity < needed )
    {
        if( capacity > UINT32_MAX / 2u )
        {
            capacity = needed;
            break;
        }
        capacity *= 2u;
    }
    grown = (uint16_t*)realloc(renderer->model_indices, (size_t)capacity * sizeof(*grown));
    assert(grown);
    renderer->model_indices = grown;
    renderer->model_index_capacity = capacity;
    return true;
}

/* ---- vertex streams ------------------------------------------------------------- */

bool
gles2_bind_stream(struct ToriRS_GLES2* renderer, uint32_t binding, uint32_t page_base)
{
    GLuint buffer;
    uint64_t base_vertex = page_base;
    uint64_t byte_offset;
    assert(renderer);
    if( binding < TRSPK_VBO_GROUP_COUNT )
    {
        buffer = renderer->groups[binding].vbo_gpu;
        base_vertex += renderer->groups[binding].gpu_base_vertex;
    }
    else if( binding == GLES2_STATIC_PAGE_BINDING )
        buffer = renderer->static_batch_vbo;
    else if( binding == GLES2_FRAME_STREAM_BINDING )
    {
        buffer = renderer->frame_stream_vbo;
        base_vertex += renderer->frame_stream_gpu_base;
    }
    else if( binding == GLES2_HOT_BINDING )
        buffer = renderer->hot_vbo;
    else
        return false;
    if( !buffer )
        return false;
    byte_offset = base_vertex * sizeof(struct TRSPK_VertexGLES2);
    assert(byte_offset <= (uint64_t)INT32_MAX);
    if( renderer->stream_layout == GLES2_STREAM_WORLD && renderer->stream_buffer == buffer &&
        renderer->stream_byte_offset == (uint32_t)byte_offset )
        return true;
    gles2_bind_array_buffer(renderer, buffer);
    glVertexAttribPointer(
        GLES2_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, GLES2_VERTEX_STRIDE,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct TRSPK_VertexGLES2, position)));
    glVertexAttribPointer(
        GLES2_ATTRIB_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, GLES2_VERTEX_STRIDE,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct TRSPK_VertexGLES2, rgba)));
    glVertexAttribPointer(
        GLES2_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, GLES2_VERTEX_STRIDE,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct TRSPK_VertexGLES2, texcoord)));
    glVertexAttribPointer(
        GLES2_ATTRIB_TEXINFO, 4, GL_UNSIGNED_BYTE, GL_FALSE, GLES2_VERTEX_STRIDE,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct TRSPK_VertexGLES2, tile_col)));
    if( renderer->stream_layout == GLES2_STREAM_ROTMASK )
        glEnableVertexAttribArray(GLES2_ATTRIB_TEXINFO);
    renderer->stream_buffer = buffer;
    renderer->stream_byte_offset = (uint32_t)byte_offset;
    renderer->stream_layout = GLES2_STREAM_WORLD;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATTRIB_REBINDS, 1);
    return true;
}

void
gles2_bind_ui_stream(struct ToriRS_GLES2* renderer, uint32_t byte_offset)
{
    const GLsizei stride = (GLsizei)sizeof(struct GLES2VertexUI);
    assert(renderer);
    if( renderer->stream_layout == GLES2_STREAM_UI && renderer->stream_buffer == renderer->ui_vbo &&
        renderer->stream_byte_offset == byte_offset )
        return;
    gles2_bind_array_buffer(renderer, renderer->ui_vbo);
    glVertexAttribPointer(
        GLES2_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct GLES2VertexUI, x)));
    glVertexAttribPointer(
        GLES2_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct GLES2VertexUI, u)));
    glVertexAttribPointer(
        GLES2_ATTRIB_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct GLES2VertexUI, rgba)));
    glVertexAttribPointer(
        GLES2_ATTRIB_TEXINFO, 1, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct GLES2VertexUI, sel)));
    if( renderer->stream_layout == GLES2_STREAM_ROTMASK )
        glEnableVertexAttribArray(GLES2_ATTRIB_TEXINFO);
    renderer->stream_buffer = renderer->ui_vbo;
    renderer->stream_byte_offset = byte_offset;
    renderer->stream_layout = GLES2_STREAM_UI;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATTRIB_REBINDS, 1);
}

void
gles2_bind_rotmask_stream(struct ToriRS_GLES2* renderer, uint32_t byte_offset)
{
    const GLsizei stride = (GLsizei)sizeof(struct GLES2VertexRotmask);
    assert(renderer);
    /* Attribute pointers are context state that outlives the draw (ES 2.0
     * has no VAO; §2.8 vertex array state persists until re-pointed), so a
     * second rotmask draw from the same offset needs no re-issue. */
    if( renderer->stream_layout == GLES2_STREAM_ROTMASK && renderer->stream_buffer == renderer->ui_vbo &&
        renderer->stream_byte_offset == byte_offset )
        return;
    gles2_bind_array_buffer(renderer, renderer->ui_vbo);
    glVertexAttribPointer(
        GLES2_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct GLES2VertexRotmask, x)));
    glVertexAttribPointer(
        GLES2_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct GLES2VertexRotmask, u)));
    glVertexAttribPointer(
        GLES2_ATTRIB_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct GLES2VertexRotmask, rgba)));
    glVertexAttribPointer(
        GLES2_ATTRIB_MASK_TEXCOORD, 2, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct GLES2VertexRotmask, mask_u)));
    /* The fourth slot is shared (the static assert at the top of this
     * file): the world's texinfo, the UI's sampler select and this
     * program's a_mask_texcoord are one attribute index, enabled at init
     * and by every layout, so it stays enabled here. An earlier layout gave
     * the mask uv its own index and switched the then-unused texinfo array
     * off for this draw (the Adreno 320 drops a draw with a stray array
     * enabled); once the slot was shared, that same switch-off disabled the
     * mask uv itself whenever the previous layout was the world's, the
     * shader read the constant (0,0) -- an opaque mask corner -- and
     * discarded every fragment: the minimap and the compass drew nothing,
     * with no GL error, on the phone. */
    glEnableVertexAttribArray(GLES2_ATTRIB_MASK_TEXCOORD);
    renderer->stream_buffer = renderer->ui_vbo;
    renderer->stream_byte_offset = byte_offset;
    renderer->stream_layout = GLES2_STREAM_ROTMASK;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATTRIB_REBINDS, 1);
}

/*
 * The 2D ring. Every flush appends at the head instead of rewriting offset
 * 0, so the driver never has to synchronise a write against the draw that is
 * still reading. Wrapping orphans the whole buffer with glBufferData(NULL),
 * which is the ES2 idiom for "give me fresh storage, keep the old for the GPU".
 * `earlier_appends_drawn`: see gles2_stream_set_append.
 */
uint32_t
gles2_ring_upload(
    struct ToriRS_GLES2* renderer,
    const void* data,
    uint32_t bytes,
    bool earlier_appends_drawn)
{
    uint32_t offset;
    assert(renderer);
    assert(data);
    offset = gles2_stream_set_append(
        &renderer->ui_stream,
        renderer->frame_slot,
        GL_ARRAY_BUFFER,
        GLES2_UI_STREAM_INIT_BYTES,
        data,
        bytes,
        earlier_appends_drawn);
    renderer->bound_array_buffer = renderer->ui_vbo;
    return offset;
}

/* ---- the world draw ------------------------------------------------------------- */

void
gles2_use_world_program(struct ToriRS_GLES2* renderer, bool cutout)
{
    const struct GLES2Program* program;
    assert(renderer);
    program = cutout ? &renderer->program_world_cutout : &renderer->program_world_plain;
    gles2_use_program(renderer, program);
    glUniformMatrix4fv(program->u_matrix, 1, GL_FALSE, renderer->model_view_projection);
    /* Reduced modulo 128 on the CPU: speed / 128 texels per tick means the
     * scroll repeats every 128 ticks, and a float clock that never grows past
     * 128 keeps the fract() in the shader exact. */
    glUniform1f(program->u_clock, (float)fmod(renderer->frame_clock, 128.0));
    gles2_bind_texture0(renderer, renderer->atlas_texture);
}

bool
gles2_upload_geometry(struct ToriRS_GLES2* renderer)
{
    uint32_t group;
    assert(renderer);
    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
        if( !gles2_upload_group(renderer, &renderer->groups[group]) )
            return false;
    return gles2_upload_dirty_static_batches(renderer);
}

/* ---- the draw sequence and the two per-frame rings -------------------------------- */

void
gles2_sequence_reset(struct ToriRS_GLES2* renderer)
{
    assert(renderer);
    renderer->draw_item_count = 0u;
    renderer->ibo_staging_count = 0u;
    renderer->frame_stream_count = 0u;
    renderer->hot_frame_oldest_serial = UINT64_MAX;
}

static struct GLES2DrawItem*
gles2_sequence_append(struct ToriRS_GLES2* renderer)
{
    if( renderer->draw_item_count >= renderer->draw_item_capacity )
    {
        uint32_t capacity = renderer->draw_item_capacity ? renderer->draw_item_capacity * 2u
                                                         : GLES2_DRAW_ITEM_INIT;
        struct GLES2DrawItem* grown = (struct GLES2DrawItem*)realloc(
            renderer->draw_items, (size_t)capacity * sizeof(*grown));
        assert(grown);
        renderer->draw_items = grown;
        renderer->draw_item_capacity = capacity;
    }
    return &renderer->draw_items[renderer->draw_item_count++];
}

void
gles2_sequence_push_indexed(
    struct ToriRS_GLES2* renderer,
    uint32_t binding,
    uint32_t page_base,
    bool cutout,
    bool blended,
    const uint16_t* indices,
    uint32_t index_count)
{
    uint16_t* destination;
    assert(renderer);
    assert(indices);
    if( index_count == 0u )
        return;
    destination = gles2_sequence_reserve_indexed(renderer, index_count);
    memcpy(destination, indices, (size_t)index_count * sizeof(*indices));
    gles2_sequence_commit_indexed(renderer, binding, page_base, cutout, blended, index_count);
}

uint16_t*
gles2_sequence_reserve_indexed(
    struct ToriRS_GLES2* renderer,
    uint32_t index_count)
{
    uint32_t needed;
    assert(renderer);
    needed = renderer->ibo_staging_count + index_count;
    if( needed > renderer->ibo_staging_capacity )
    {
        uint32_t capacity = renderer->ibo_staging_capacity ? renderer->ibo_staging_capacity
                                                           : GLES2_GPU_BUFFER_INIT;
        uint16_t* grown;
        while( capacity < needed )
            capacity *= 2u;
        grown = (uint16_t*)realloc(renderer->ibo_staging, (size_t)capacity * sizeof(*grown));
        assert(grown);
        renderer->ibo_staging = grown;
        renderer->ibo_staging_capacity = capacity;
    }
    return renderer->ibo_staging + renderer->ibo_staging_count;
}

void
gles2_sequence_commit_indexed(
    struct ToriRS_GLES2* renderer,
    uint32_t binding,
    uint32_t page_base,
    bool cutout,
    bool blended,
    uint32_t index_count)
{
    struct GLES2DrawItem* item;
    uint32_t needed;
    assert(renderer);
    if( index_count == 0u )
        return;
    needed = renderer->ibo_staging_count + index_count;
    assert(needed <= renderer->ibo_staging_capacity);
    /* Merge with the item before it when nothing about the draw changed. */
    item = renderer->draw_item_count ? &renderer->draw_items[renderer->draw_item_count - 1u]
                                     : NULL;
    if( item && item->indexed && item->binding == binding && item->page_base == page_base &&
        item->cutout == (uint8_t)cutout && item->blended == (uint8_t)blended &&
        item->first + item->count == renderer->ibo_staging_count )
        item->count += index_count;
    else
    {
        item = gles2_sequence_append(renderer);
        item->binding = binding;
        item->page_base = page_base;
        item->first = renderer->ibo_staging_count;
        item->count = index_count;
        item->indexed = 1u;
        item->cutout = (uint8_t)cutout;
        item->blended = (uint8_t)blended;
    }
    renderer->ibo_staging_count = needed;
}

void
gles2_sequence_push_array(
    struct ToriRS_GLES2* renderer,
    uint32_t binding,
    uint32_t first,
    uint32_t count,
    bool cutout,
    bool blended)
{
    struct GLES2DrawItem* item;
    assert(renderer);
    if( count == 0u )
        return;
    item = renderer->draw_item_count ? &renderer->draw_items[renderer->draw_item_count - 1u]
                                     : NULL;
    if( item && !item->indexed && item->binding == binding && item->cutout == (uint8_t)cutout &&
        item->blended == (uint8_t)blended && item->first + item->count == first )
    {
        item->count += count;
        return;
    }
    item = gles2_sequence_append(renderer);
    item->binding = binding;
    item->page_base = 0u;
    item->first = first;
    item->count = count;
    item->indexed = 0u;
    item->cutout = (uint8_t)cutout;
    item->blended = (uint8_t)blended;
}

uint32_t
gles2_frame_stream_reserve(struct ToriRS_GLES2* renderer, uint32_t vertex_count)
{
    uint32_t first;
    assert(renderer);
    assert(renderer->frame_stream_cpu);
    first = renderer->frame_stream_count;
    trspk_vbo_ensure_capacity(renderer->frame_stream_cpu, first + vertex_count);
    trspk_triangles_ensure(&renderer->frame_stream_triangles, (first + vertex_count) / 3u + 1u);
    renderer->frame_stream_count = first + vertex_count;
    return first;
}

static void
gles2_frame_stream_upload(struct ToriRS_GLES2* renderer)
{
    uint32_t bytes;
    uint32_t offset;
    if( renderer->frame_stream_count == 0u )
        return;
    bytes = renderer->frame_stream_count * (uint32_t)sizeof(struct TRSPK_VertexGLES2);
    offset = gles2_stream_set_append(
        &renderer->frame_stream,
        renderer->frame_slot,
        GL_ARRAY_BUFFER,
        GLES2_FRAME_STREAM_INIT_BYTES,
        renderer->frame_stream_cpu->vertices.as_gles2,
        bytes,
        false);
    renderer->bound_array_buffer = renderer->frame_stream_vbo;
    renderer->frame_stream_gpu_base = offset / (uint32_t)sizeof(struct TRSPK_VertexGLES2);
    /* The stream just moved; the attribute pointers must follow it. */
    if( renderer->stream_buffer == renderer->frame_stream_vbo )
        renderer->stream_layout = GLES2_STREAM_NONE;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DYNAMIC_VBO_UPLOAD_BYTES, (int64_t)bytes);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DYNAMIC_VBO_UPLOADS, 1);
}

void
gles2_sequence_draw(struct ToriRS_GLES2* renderer)
{
    uint32_t index_base_bytes = 0u;
    uint32_t item_index;
    uint32_t draw_calls = 0u;
    int program_cutout = -1;
    int pass_blended = -1;

    assert(renderer);
    if( renderer->draw_item_count == 0u )
        return;
    if( renderer->ibo_staging_count > 0u )
    {
        uint32_t bytes = renderer->ibo_staging_count * (uint32_t)sizeof(uint16_t);
        index_base_bytes = gles2_stream_set_append(
            &renderer->index_stream,
            renderer->frame_slot,
            GL_ELEMENT_ARRAY_BUFFER,
            GLES2_INDEX_STREAM_INIT_BYTES,
            renderer->ibo_staging,
            bytes,
            false);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_IBO_UPLOAD_BYTES, (int64_t)bytes);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_IBO_UPLOADS, 1);
    }
    if( renderer->zbuffer )
        gles2_zbuffer_apply_world_states(renderer);
    else
        gles2_painter_apply_world_states(renderer);

    for( item_index = 0u; item_index < renderer->draw_item_count; item_index++ )
    {
        const struct GLES2DrawItem* item = &renderer->draw_items[item_index];
        if( renderer->zbuffer && pass_blended != (int)item->blended )
        {
            gles2_zbuffer_apply_pass_states(renderer, item->blended != 0u);
            pass_blended = (int)item->blended;
        }
        if( program_cutout != (int)item->cutout )
        {
            gles2_use_world_program(renderer, item->cutout != 0u);
            program_cutout = (int)item->cutout;
        }
        if( !gles2_bind_stream(renderer, item->binding, item->indexed ? item->page_base : 0u) )
            continue;
        if( item->indexed )
            glDrawElements(
                GL_TRIANGLES,
                (GLsizei)item->count,
                GL_UNSIGNED_SHORT,
                (const void*)(uintptr_t)(index_base_bytes + (size_t)item->first * sizeof(uint16_t)));
        else
            glDrawArrays(GL_TRIANGLES, (GLint)item->first, (GLsizei)item->count);
        draw_calls++;
    }
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, draw_calls);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_RANGES, renderer->draw_item_count);
}

/* ---- the 3D pass ------------------------------------------------------------------- */

static void
gles2_mat4_multiply(const float* a, const float* b, float* out)
{
    int column;
    int row;
    int k;
    for( column = 0; column < 4; column++ )
        for( row = 0; row < 4; row++ )
        {
            float sum = 0.0f;
            for( k = 0; k < 4; k++ )
                sum += a[k * 4 + row] * b[column * 4 + k];
            out[column * 4 + row] = sum;
        }
}

static void
gles2_begin_3d(struct ToriRS_GLES2* renderer, const struct ToriRS_RenderCommand_Begin3D* command)
{
    const struct ToriDraw_ViewPort* viewport;
    int pass_w;
    int pass_h;
    int logical_x;
    int logical_y;
    int left;
    int top;
    int right;
    int bottom;
    uint32_t group;

    assert(renderer);
    assert(command);
    if( !renderer->gl_context )
        return;
    gles2_sequence_reset(renderer);
    renderer->current_3d = *command;
    renderer->has_3d = true;
    renderer->in3d = true;
    /* Publish the prepared camera block: the prepared projection kernels are
     * gated on this pointer being the one the projection is called with. */
    if( renderer->scene )
        ToriDraw_ScenePrepareProjectionCamera(renderer->scene, &renderer->current_3d.camera);
    /* Every scene event of the frame has been dispatched by now (the frame
     * drains them before its first non-event command): a stage source may
     * start reading and posing models. */
    if( renderer->model_stage_source && renderer->model_stage_source->begin_3d )
        renderer->model_stage_source->begin_3d(renderer->model_stage_source->user, command);

    viewport = &renderer->current_3d.view_port;
    pass_w = viewport->width > 0 ? viewport->width : renderer->width;
    pass_h = viewport->height > 0 ? viewport->height : renderer->height;
    logical_x = viewport->x_center - pass_w / 2;
    logical_y = viewport->y_center - pass_h / 2;
    left = renderer->letterbox_x + (int)((int64_t)logical_x * renderer->letterbox_width / renderer->width);
    top = renderer->letterbox_y + (int)((int64_t)logical_y * renderer->letterbox_height / renderer->height);
    right = renderer->letterbox_x +
        (int)((int64_t)(logical_x + pass_w) * renderer->letterbox_width / renderer->width);
    bottom = renderer->letterbox_y +
        (int)((int64_t)(logical_y + pass_h) * renderer->letterbox_height / renderer->height);
    if( right <= left )
        right = left + 1;
    if( bottom <= top )
        bottom = top + 1;
    renderer->world_viewport.x = left;
    renderer->world_viewport.y = renderer->drawable_height - bottom;
    renderer->world_viewport.width = right - left;
    renderer->world_viewport.height = bottom - top;
    glViewport(
        renderer->world_viewport.x,
        renderer->world_viewport.y,
        renderer->world_viewport.width,
        renderer->world_viewport.height);
    if( renderer->zbuffer )
        gles2_zbuffer_begin_pass(renderer);

    trspk_compute_pass_matrices(
        renderer->view,
        renderer->projection,
        (float)command->camera_position.x,
        (float)command->camera_position.y,
        (float)command->camera_position.z,
        ToriDraw_AngleToRadians(command->camera.pitch),
        ToriDraw_AngleToRadians(command->camera.yaw),
        pass_w,
        pass_h,
        (int)command->camera.projection_mode,
        command->camera.projection_scale,
        command->camera.fov_rpi2048,
        command->camera.parallel_zoom16);
    if( renderer->zbuffer )
        gles2_zbuffer_setup_projection(renderer, command);
    else
        gles2_painter_setup_projection(renderer);
    gles2_mat4_multiply(renderer->projection, renderer->view, renderer->model_view_projection);

    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
        if( renderer->groups[group].reset_each_frame )
            gles2_reset_group(&renderer->groups[group]);
}

static void
gles2_draw_model(struct ToriRS_GLES2* renderer, const struct ToriRS_RenderCommand_Model* command)
{
    struct ToriDraw_Position projected_position;
    struct GLES2ModelPlacement placement;
    struct GLES2ModelStage stage;
    bool staged = false;
    const int* face_order;
    bool projected_in_scene;
    int projected_depth;
    int face_count;
    int sorted_face_count = 0;
    bool dynamic;
    int anim_index;
    int pose_id;
    uint32_t vertex_base;
    uint32_t page_base = 0u;
    uint32_t local_base;
    uint32_t binding;
    uint32_t group;

    assert(renderer);
    assert(command);
    /* The stage source, when one is installed, is asked about EVERY model
     * command before any early return: it hands results out in dispatch
     * order and pairs them with the asks by count. */
    if( renderer->model_stage_source )
        staged = renderer->model_stage_source->take(
            renderer->model_stage_source->user, command, &stage);
    if( !renderer->has_3d || !renderer->scene || command->model.kind == TORIDRAWMK_NONE )
        return;
    placement.page_id = UINT32_MAX;
    placement.batch_slot = UINT32_MAX;
    placement.entry_index = UINT32_MAX;
    placement.entry_vertex_count = 0u;
    if( staged )
    {
        /* Pose, cull, projection, pick test and sort were done by the source
         * (the dual-core lane's worker, on its own scratch view of the
         * scene); this thread consumes. The pose the source applied is the
         * one the bakes below read -- its results were published after it. */
        if( stage.cull != TORIDRAW_CULL_VISIBLE )
            return;
        if( renderer->pick_enabled && command->pickable && command->element_id >= 0 &&
            stage.pick_hit )
            ToriRS_PickHitsAdd(
                &renderer->pick_hits,
                command->element_id,
                command->pick_terrain,
                command->pick_tile_x,
                command->pick_tile_z,
                command->pick_tile_level,
                command->pick_view);
        if( command->pick_only )
            return;
        if( renderer->zbuffer )
        {
            face_count = trspk_toridraw_face_count(command->model);
            sorted_face_count = stage.sorted ? stage.sorted_face_count : 0;
        }
        else
        {
            /* The painter draws sorted faces and nothing else; an unsorted
             * stage here is the producer's bug, not a case. */
            assert(stage.sorted);
            face_count = stage.sorted_face_count;
            sorted_face_count = face_count;
        }
        face_order = stage.sorted ? stage.face_order : NULL;
        projected_depth = stage.projected_depth;
        projected_in_scene = false;
    }
    else
    {
        if( command->animation && command->element_id >= 0 )
            ToriDraw_SceneElementApplyAnimation(
                renderer->scene,
                command->element_id,
                command->anim_index == 0,
                command->anim_frame);
        projected_position = command->position;
        if( ToriDraw_RenderModel1ProjectWithTable(
                command->model,
                renderer->scene,
                &projected_position,
                &renderer->current_3d.view_port,
                &renderer->current_3d.camera,
                renderer->kernel) != TORIDRAW_CULL_VISIBLE )
            return;

        if( renderer->pick_enabled && command->pickable && command->element_id >= 0 &&
            (command->pick_aabb
                 ? ToriDraw_ProjectedModelContainsAabb(
                       renderer->scene, renderer->pick_mouse_x, renderer->pick_mouse_y)
                 : command->pick_terrain
                     ? ToriDraw_ProjectedTileMouseHitTest(
                           renderer->scene,
                           command->model,
                           &renderer->current_3d.view_port,
                           renderer->pick_mouse_x,
                           renderer->pick_mouse_y)
                     : ToriDraw_ProjectedModelMouseHitTest(
                           renderer->scene,
                           command->model,
                           &renderer->current_3d.view_port,
                           renderer->pick_mouse_x,
                           renderer->pick_mouse_y)) )
            ToriRS_PickHitsAdd(
                &renderer->pick_hits,
                command->element_id,
                command->pick_terrain,
                command->pick_tile_x,
                command->pick_tile_z,
                command->pick_tile_level,
                command->pick_view);
        if( command->pick_only )
            return;

        /* The depth path classifies per face during emission and needs no
         * order up front; the painter path must sort before it can count. */
        face_count = renderer->zbuffer
            ? trspk_toridraw_face_count(command->model)
            : gles2_painter_sort_faces(renderer, command, &sorted_face_count);
        face_order = ToriDraw_FaceOrder(renderer->scene);
        projected_depth = renderer->scene->projected_vertex.z;
        projected_in_scene = true;
    }
    /* The sort census is a debug readout (TORIRS_GLES2_DEBUG); it costs a
     * second trspk_toridraw_face_count per model, so it is gated where it
     * is gathered, not only where it is printed. */
    if( !renderer->zbuffer && renderer->debug )
    {
        int model_faces = trspk_toridraw_face_count(command->model);
        int bucket = model_faces <= 2 ? 0 : model_faces <= 16 ? 1 : model_faces <= 64 ? 2
                                                              : model_faces <= 256 ? 3 : 4;
        renderer->painter_stat_sort_models[bucket]++;
        renderer->painter_stat_sort_faces_in += (uint32_t)(model_faces > 0 ? model_faces : 0);
        renderer->painter_stat_sort_faces_out += (uint32_t)(face_count > 0 ? face_count : 0);
    }
    if( face_count <= 0 || (uint32_t)face_count > UINT32_MAX / 3u )
        return;
    dynamic = command->dynamic || command->element_id < 0;
    anim_index = gles2_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1);
    pose_id = command->animation && command->anim_frame >= 0 ? command->anim_frame : 0;
    if( command->animation && command->animation->frame_count > 0 &&
        pose_id >= command->animation->frame_count )
        pose_id = 0;
    group = dynamic ? TRSPK_VBO_GROUP_DYNAMIC : TRSPK_VBO_GROUP_STATIC;
    binding = group;
    if( dynamic && !renderer->zbuffer )
    {
        /* Painter path: an actor is baked straight into the frame stream in
         * its sorted face order, so it needs neither the dynamic arena nor an
         * index. The placement names the stream and the sorted count. */
        uint32_t first = gles2_frame_stream_reserve(renderer, (uint32_t)sorted_face_count * 3u);
        if( !gles2_bake_pose_vertices(
                renderer,
                renderer->frame_stream_cpu,
                &renderer->frame_stream_triangles,
                first,
                command->model,
                &command->world_position,
                face_order,
                sorted_face_count) )
            return;
        placement.face_order = face_order;
        placement.projected_in_scene = projected_in_scene;
        placement.projected_depth = projected_depth;
        placement.binding = GLES2_FRAME_STREAM_BINDING;
        placement.page_base = 0u;
        placement.local_base = first;
        placement.absolute_base = first;
        placement.face_count = face_count;
        placement.sorted_face_count = sorted_face_count;
        placement.anim_index = anim_index;
        placement.pose_id = pose_id;
        placement.dynamic = true;
        gles2_painter_emit_model(renderer, &placement);
        return;
    }
    if( dynamic )
    {
        vertex_base = gles2_bake_into_arena(
            renderer,
            &renderer->groups[group],
            command->element_id,
            anim_index,
            pose_id,
            command->model,
            &command->world_position,
            false);
    }
    else if( trspk_pose_table_get(
                 &renderer->batch_poses, command->element_id, anim_index, pose_id, &vertex_base) )
    {
        uint32_t batch_slot;
        uint32_t entry_index;
        uint32_t page_id;
        const struct GLES2StaticBatch* batch;
        const struct TRSPK_Batch16Entry* entry;
        if( (vertex_base & GLES2_BATCH_POSE_FLAG) == 0u )
            return;
        batch_slot = (vertex_base >> GLES2_BATCH_POSE_SLOT_SHIFT) & GLES2_BATCH_POSE_SLOT_MASK;
        entry_index = vertex_base & GLES2_BATCH_POSE_ENTRY_MASK;
        if( batch_slot >= renderer->static_batch_count )
            return;
        batch = &renderer->static_batches[batch_slot];
        if( !batch->active || !batch->cpu )
            return;
        entry = trspk_batch16_get_entry(batch->cpu, entry_index);
        if( !entry || entry->chunk_index >= batch->page_id_capacity )
            return;
        page_id = batch->page_ids[entry->chunk_index];
        if( page_id >= renderer->static_page_count || !renderer->static_pages[page_id].valid )
            return;
        binding = GLES2_STATIC_PAGE_BINDING;
        page_base = renderer->static_pages[page_id].gpu_offset;
        placement.page_id = page_id;
        placement.batch_slot = batch_slot;
        placement.entry_index = entry_index;
        placement.entry_vertex_count = entry->vertex_count;
        vertex_base = entry->vertex_base;
    }
    else if( !trspk_pose_table_get(
                 &renderer->poses, command->element_id, anim_index, pose_id, &vertex_base) )
    {
        /* A live static element without its load event (renderer creation,
         * an event-queue overflow): bake the complete animation once on the
         * first miss, not one pose every frame. */
        if( command->animation )
        {
            struct ToriRS_RenderCommand_AnimLoad load;
            memset(&load, 0, sizeof(load));
            load.element_id = command->element_id;
            load.anim_index = anim_index;
            load.animation = command->animation;
            load.model = command->model;
            load.world_position = command->world_position;
            gles2_animation_load(renderer, &load);
        }
        if( !trspk_pose_table_get(
                &renderer->poses, command->element_id, anim_index, pose_id, &vertex_base) )
            vertex_base = gles2_bake_into_arena(
                renderer,
                &renderer->groups[group],
                command->element_id,
                anim_index,
                pose_id,
                command->model,
                &command->world_position,
                true);
    }
    if( vertex_base == UINT32_MAX )
        return;

    if( binding < GLES2_STATIC_PAGE_BINDING )
    {
        page_base = vertex_base & ~(GLES2_VBO_PAGE - 1u);
        local_base = vertex_base - page_base;
    }
    else
        local_base = vertex_base;
    /* model_indices is the depth path's per-model index scratch; the painter
     * writes its indices straight into the sequence staging and never reads
     * it. */
    if( renderer->zbuffer && !gles2_reserve_model_indices(renderer, (uint32_t)face_count * 3u) )
        return;

    placement.binding = binding;
    placement.page_base = page_base;
    placement.local_base = local_base;
    placement.absolute_base = page_base + local_base;
    placement.face_count = face_count;
    placement.sorted_face_count = sorted_face_count;
    placement.anim_index = anim_index;
    placement.pose_id = pose_id;
    placement.dynamic = dynamic;
    placement.face_order = face_order;
    placement.projected_in_scene = projected_in_scene;
    placement.projected_depth = projected_depth;
    if( renderer->zbuffer )
        gles2_zbuffer_emit_model(renderer, command, &placement);
    else
        gles2_painter_emit_model(renderer, &placement);
}

static void
gles2_set_letterbox_viewport(struct ToriRS_GLES2* renderer)
{
    glViewport(
        renderer->letterbox_x,
        renderer->letterbox_y,
        renderer->letterbox_width,
        renderer->letterbox_height);
}

static void
gles2_end_3d(struct ToriRS_GLES2* renderer)
{
    assert(renderer);
    /* The prepared block describes a camera about to go out of scope;
     * unpublishing it is what stops a later pass reading a stale one. */
    if( renderer->scene )
        ToriDraw_SceneClearProjectionCamera(renderer->scene);
    if( !renderer->has_3d )
        goto done;
    if( !gles2_upload_atlas(renderer) )
        goto done;
    if( renderer->zbuffer )
    {
        /* The retained world is drawn from the GPU: push what changed, then
         * the opaque and the blended halves of the sequence. */
        gles2_zbuffer_flush_opaque(renderer);
        if( !gles2_upload_geometry(renderer) )
            goto done;
        gles2_zbuffer_end_pass(renderer);
    }
    else
    {
        /* The painter path draws the static world from its resident window
         * and everything else from the frame stream; the retained GPU pages
         * are never read here and never uploaded. */
        gles2_painter_flush(renderer);
        gles2_frame_stream_upload(renderer);
    }
    gles2_sequence_draw(renderer);
    if( !renderer->zbuffer )
    {
        bool debug = renderer->debug;
        renderer->painter_stat_frames++;
        renderer->painter_stat_draws += renderer->draw_item_count;
        if( debug && renderer->painter_stat_frames == 300u )
        {
            gles2_report_line(
                "gles2 painter/frame: faces indexed %.0f gathered %.0f actor %.0f; residents "
                "placed %.1f models %.0f vertices, serial hits %.0f; draws %.1f; compactions %u "
                "(held back %u frames, %u since last); ring head %llu; "
                "static pages %u %u vertices",
                renderer->painter_stat_faces_indexed / 300.0,
                renderer->painter_stat_faces_gathered / 300.0,
                renderer->painter_stat_faces_actor / 300.0,
                renderer->painter_stat_placed_models / 300.0,
                renderer->painter_stat_placed_vertices / 300.0,
                renderer->painter_stat_resident_hits / 300.0,
                renderer->painter_stat_draws / 300.0,
                renderer->painter_stat_compactions,
                renderer->painter_stat_compactions_deferred,
                renderer->hot_frames_since_compaction,
                (unsigned long long)renderer->hot_head,
                renderer->static_page_count,
                renderer->static_batch_gpu_vertex_used);
            gles2_report_line(
                "gles2 sort/frame: models by bake size tile2 %.0f <=16 %.0f <=64 %.0f <=256 %.0f "
                "larger %.0f; faces in %.0f out %.0f; radix shallow %.1f two-pass %.1f; "
                "prio uniform %.1f varied %.1f; k16 %.1f declined %.1f",
                renderer->painter_stat_sort_models[0] / 300.0,
                renderer->painter_stat_sort_models[1] / 300.0,
                renderer->painter_stat_sort_models[2] / 300.0,
                renderer->painter_stat_sort_models[3] / 300.0,
                renderer->painter_stat_sort_models[4] / 300.0,
                renderer->painter_stat_sort_faces_in / 300.0,
                renderer->painter_stat_sort_faces_out / 300.0,
                g_toridraw_radix_shallow_models / 300.0,
                g_toridraw_radix_two_pass_models / 300.0,
                g_toridraw_prio_uniform_models / 300.0,
                g_toridraw_prio_varied_models / 300.0,
                g_toridraw_sort_k16_models / 300.0,
                g_toridraw_sort_k16_declined / 300.0);
            gles2_report_line(
                "gles2 draws/frame: world %.1f; ui batches %.1f (ended by texture %.1f atlas "
                "%.1f scissor %.1f overflow %.1f asked %.1f) rotmask %.1f widget %.1f; ui "
                "upload %.0f B",
                renderer->painter_stat_draws / 300.0,
                renderer->ui_stat_draws_batch / 300.0,
                renderer->ui_stat_break_texture / 300.0,
                renderer->ui_stat_break_atlas / 300.0,
                renderer->ui_stat_break_scissor / 300.0,
                renderer->ui_stat_break_overflow / 300.0,
                ((double)renderer->ui_stat_draws_batch - renderer->ui_stat_break_texture -
                 renderer->ui_stat_break_atlas - renderer->ui_stat_break_scissor -
                 renderer->ui_stat_break_overflow) /
                    300.0,
                renderer->ui_stat_draws_rotmask / 300.0,
                renderer->ui_stat_draws_widget / 300.0,
                renderer->ui_stat_upload_bytes / 300.0);
            gles2_report_line(
                "project/frame: models %.1f cull_fast %.1f cull_aabb %.1f error %.1f projected "
                "%.1f vertices %.0f tail_models %.1f",
                g_toridraw_project_census.calls / 300.0,
                g_toridraw_project_census.cull_fast / 300.0,
                g_toridraw_project_census.cull_aabb / 300.0,
                g_toridraw_project_census.cull_error / 300.0,
                g_toridraw_project_census.projected / 300.0,
                g_toridraw_project_census.projected_vertices / 300.0,
                g_toridraw_project_census.tail_models / 300.0);
            gles2_report_line(
                "paint/frame: walks %.2f same_inputs %.2f pops %.0f commands %.0f entities %.1f",
                g_torirs_paint_census.walks / 300.0,
                g_torirs_paint_census.same_inputs / 300.0,
                g_torirs_paint_census.pops / 300.0,
                g_torirs_paint_census.commands / 300.0,
                g_torirs_paint_census.entity_commands / 300.0);
            /* A call-site counting shim, when one was built in with -include
             * (scratch tooling; the symbol is absent in every normal build). */
            {
                extern void torirs_shim_dump(void) __attribute__((weak));
                if( torirs_shim_dump )
                    torirs_shim_dump();
            }
        }
        if( renderer->painter_stat_frames >= 300u )
        {
            renderer->painter_stat_frames = 0u;
            renderer->painter_stat_faces_indexed = 0u;
            renderer->painter_stat_faces_gathered = 0u;
            renderer->painter_stat_faces_actor = 0u;
            renderer->painter_stat_placed_models = 0u;
            renderer->painter_stat_placed_vertices = 0u;
            renderer->painter_stat_resident_hits = 0u;
            renderer->painter_stat_draws = 0u;
            memset(
                renderer->painter_stat_sort_models, 0, sizeof(renderer->painter_stat_sort_models));
            renderer->painter_stat_sort_faces_in = 0u;
            renderer->painter_stat_sort_faces_out = 0u;
            g_toridraw_radix_shallow_models = 0;
            g_toridraw_radix_two_pass_models = 0;
            g_toridraw_prio_uniform_models = 0;
            g_toridraw_prio_varied_models = 0;
            g_toridraw_sort_k16_models = 0;
            g_toridraw_sort_k16_declined = 0;
            renderer->ui_stat_draws_batch = 0u;
            renderer->ui_stat_draws_rotmask = 0u;
            renderer->ui_stat_draws_widget = 0u;
            renderer->ui_stat_break_texture = 0u;
            renderer->ui_stat_break_atlas = 0u;
            renderer->ui_stat_break_scissor = 0u;
            renderer->ui_stat_break_overflow = 0u;
            renderer->ui_stat_upload_bytes = 0u;
            memset(&g_toridraw_project_census, 0, sizeof(g_toridraw_project_census));
            memset(&g_torirs_paint_census, 0, sizeof(g_torirs_paint_census));
        }
    }

done:
    renderer->has_3d = false;
    renderer->in3d = false;
    gles2_sequence_reset(renderer);
    if( renderer->zbuffer )
        gles2_zbuffer_reset_pass(renderer);
    /* The world pass leaves a world-sized viewport and depth state; restore
     * so 2D that follows is neither clipped nor occluded. */
    gles2_set_letterbox_viewport(renderer);
    gles2_set_depth(renderer, false, false);
    gles2_set_cull(renderer, false);
    gles2_set_scissor(renderer, NULL);
}

/* ---- batch commands --------------------------------------------------------------- */

static void
gles2_batch_begin(struct ToriRS_GLES2* renderer, const struct ToriRS_RenderCommand_Batch* command)
{
    struct GLES2StaticBatch* batch;
    int slot;
    assert(renderer);
    assert(command);
    if( command->batch_id < 0 )
        return;
    slot = gles2_static_batch_slot(renderer, command->batch_id, true);
    if( slot < 0 )
        return;
    batch = &renderer->static_batches[slot];
    gles2_zbuffer_batch_dropped(renderer, batch->cpu);
    gles2_painter_batch_reset(renderer, batch, 0u);
    gles2_invalidate_batch_pages(renderer, batch);
    trspk_batch16_begin(batch->cpu);
    batch->active = false;
    batch->building = true;
    gles2_rebuild_batch_pose_table(renderer);
    renderer->current_batch_slot = slot;
}

static void
gles2_batch_add(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_Batch* command,
    bool animated)
{
    struct GLES2StaticBatch* batch;
    struct TRSPK_Batch16Reservation reservation;
    int anim_index;
    int pose_id;
    int face_count;
    assert(renderer);
    assert(command);
    if( command->element_id < 0 || command->model.kind == TORIDRAWMK_NONE )
        return;
    if( renderer->current_batch_slot < 0 ||
        (uint32_t)renderer->current_batch_slot >= renderer->static_batch_count )
        return;
    batch = &renderer->static_batches[renderer->current_batch_slot];
    if( !batch->building || batch->batch_id != command->batch_id )
        return;
    face_count = trspk_toridraw_face_count(command->model);
    if( face_count <= 0 || (uint32_t)face_count > UINT32_MAX / 3u )
        return;
    anim_index = animated ? gles2_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1) : 0;
    pose_id = command->pose_id >= 0 ? command->pose_id : 0;
    if( !trspk_batch16_reserve_pose(
            batch->cpu,
            command->element_id,
            anim_index,
            pose_id,
            (uint32_t)face_count * 3u,
            &reservation) )
        return;
    if( gles2_bake_pose_vertices(
            renderer,
            reservation.vbo,
            reservation.triangles,
            reservation.vertex_base,
            command->model,
            &command->world_position,
            NULL,
            0) )
        gles2_zbuffer_batch_pose_baked(
            renderer, command->element_id, anim_index, pose_id, command->model);
}

static void
gles2_batch_end(struct ToriRS_GLES2* renderer, const struct ToriRS_RenderCommand_Batch* command)
{
    struct GLES2StaticBatch* batch;
    int slot;
    assert(renderer);
    assert(command);
    slot = renderer->current_batch_slot;
    if( slot < 0 || (uint32_t)slot >= renderer->static_batch_count )
        return;
    batch = &renderer->static_batches[slot];
    if( !batch->building || batch->batch_id != command->batch_id )
        return;
    trspk_batch16_end(batch->cpu);
    batch->building = false;
    (void)gles2_static_batch_commit(renderer, (uint32_t)slot);
    renderer->current_batch_slot = -1;
}

static void
gles2_batch_clear(struct ToriRS_GLES2* renderer, int batch_id, bool clear_all)
{
    uint32_t slot;
    assert(renderer);
    for( slot = 0u; slot < renderer->static_batch_count; slot++ )
    {
        struct GLES2StaticBatch* batch = &renderer->static_batches[slot];
        if( !clear_all && batch->batch_id != batch_id )
            continue;
        gles2_zbuffer_batch_dropped(renderer, batch->cpu);
        gles2_painter_batch_reset(renderer, batch, 0u);
        gles2_invalidate_batch_pages(renderer, batch);
        trspk_batch16_clear(batch->cpu);
        batch->active = false;
        batch->building = false;
    }
    if( clear_all )
    {
        /* Nothing valid remains, so the bump allocator starts over: every
         * page re-allocates its range the next time its chunk commits. */
        uint32_t page_id;
        for( page_id = 0u; page_id < renderer->static_page_count; page_id++ )
            renderer->static_pages[page_id].gpu_capacity = 0u;
        renderer->static_batch_gpu_vertex_used = 0u;
    }
    gles2_rebuild_batch_pose_table(renderer);
    renderer->current_batch_slot = -1;
}

/* ---- dispatch ---------------------------------------------------------------------- */

static void
gles2_dispatch(struct ToriRS_GLES2* renderer, const struct ToriRS_RenderCommand* command)
{
    assert(renderer);
    assert(command);
    switch( command->kind )
    {
    case TORIRSRC_BEGIN_3D:
        gles2_begin_3d(renderer, &command->u.begin_3d);
        break;
    case TORIRSRC_END_3D:
        gles2_end_3d(renderer);
        break;
    case TORIRSRC_BEGIN_2D:
        gles2_begin_2d(renderer);
        break;
    case TORIRSRC_END_2D:
        gles2_end_2d(renderer);
        break;
    case TORIRSRC_TEX_LOAD:
        if( command->u.tex_load.texture )
            (void)gles2_load_texture_object(
                renderer, command->u.tex_load.texture_id, command->u.tex_load.texture);
        break;
    case TORIRSRC_TEX_UNLOAD:
        gles2_unload_texture(renderer, command->u.tex_load.texture_id);
        break;
    case TORIRSRC_MODEL_LOAD:
        gles2_model_load(renderer, &command->u.model_load);
        break;
    case TORIRSRC_MODEL_UNLOAD:
        gles2_model_unload(renderer, command->u.model_load.element_id);
        break;
    case TORIRSRC_ANIM_LOAD:
        gles2_animation_load(renderer, &command->u.anim_load);
        break;
    case TORIRSRC_ANIM_UNLOAD:
        gles2_animation_track_unload(
            renderer, command->u.anim_load.element_id, command->u.anim_load.anim_index);
        break;
    case TORIRSRC_BATCH3D_BEGIN:
        gles2_batch_begin(renderer, &command->u.batch);
        break;
    case TORIRSRC_BATCH3D_MODEL_ADD:
        gles2_batch_add(renderer, &command->u.batch, false);
        break;
    case TORIRSRC_BATCH3D_ANIM_ADD:
        gles2_batch_add(renderer, &command->u.batch, true);
        break;
    case TORIRSRC_BATCH3D_END:
        gles2_batch_end(renderer, &command->u.batch);
        break;
    case TORIRSRC_BATCH3D_CLEAR:
        gles2_batch_clear(renderer, command->u.batch.batch_id, command->u.batch.clear_all);
        if( command->u.batch.clear_all )
        {
            trspk_pose_table_clear(&renderer->poses);
            gles2_reset_group(&renderer->groups[TRSPK_VBO_GROUP_STATIC]);
        }
        break;
    case TORIRSRC_DRAW_MODEL:
        gles2_draw_model(renderer, &command->u.model);
        break;

    case TORIRSRC_CLEAR_RECT:
        gles2_ui_draw_clear_rect(renderer, &command->u.clear_rect);
        break;
    case TORIRSRC_FILL_RECT:
        gles2_ui_draw_fill_rect(renderer, &command->u.fill_rect);
        break;
    case TORIRSRC_DRAW_MODEL_WIDGET:
        gles2_ui_draw_model_widget(renderer, &command->u.model_widget);
        break;
    case TORIRSRC_SPRITE:
        gles2_ui_draw_sprite(renderer, &command->u.sprite);
        break;
    case TORIRSRC_FONT:
        gles2_ui_draw_font(renderer, &command->u.font);
        break;
    case TORIRSRC_LINE:
        gles2_ui_draw_line(renderer, &command->u.line);
        break;
    case TORIRSRC_POLYGON_BEGIN:
        gles2_ui_polygon_begin(renderer, &command->u.polygon_begin);
        break;
    case TORIRSRC_POLYGON_POINT:
        gles2_ui_polygon_point(renderer, &command->u.polygon_point);
        break;
    case TORIRSRC_POLYGON_END:
        gles2_ui_polygon_end(renderer);
        break;
    case TORIRSRC_TEX_BEGIN:
    case TORIRSRC_TEX_END:
    case TORIRSRC_SPRITE_BEGIN:
    case TORIRSRC_SPRITE_END:
    case TORIRSRC_FONT_BEGIN:
    case TORIRSRC_FONT_END:
        break;
    case TORIRSRC_SPRITE_LOAD:
        /* The scene owns pixels. Upload stays lazy so assets never drawn by
         * this backend consume atlas space or transfer bandwidth. */
        break;
    case TORIRSRC_SPRITE_UNLOAD:
        gles2_ui_sprite_invalidate(renderer, command->u.sprite_load.element_id);
        break;
    case TORIRSRC_FONT_LOAD:
        gles2_ui_font_load(renderer, command->u.font_load.font_id, command->u.font_load.font);
        break;
    case TORIRSRC_FONT_UNLOAD:
        gles2_ui_font_unload(renderer, command->u.font_load.font_id);
        break;
    case TORIRSRC_NONE:
        break;
    }
}

/* ---- lifetime ----------------------------------------------------------------------- */

/* A lever's environment switch: unset or anything but "0" is on. */
static bool
gles2_lever_enabled(const char* name)
{
    const char* value;
    assert(name);
    value = getenv(name);
    return !(value && value[0] == '0' && value[1] == '\0');
}

/*
 * The rotmask source generation. The sprites a rotmask slot draws from (the
 * minimap bake, UITREE_SCENE_WORLD_MAP_SPRITE_ID) are rewritten IN PLACE by
 * app_rebuild_world_map with no event the renderer sees; the renderer used to
 * discover a rewrite by hashing the whole 512x512 bake every eighth frame.
 * The producer knows when it rewrote, so it says so: a bump here, and every
 * rotmask slot re-uploads on its next draw. Process-wide rather than per
 * renderer because the caller (app.c) holds no renderer.
 */
static uint32_t g_gles2_rotmask_source_generation = 1u;

void
ToriRS_GLES2_RotmaskSourceChanged(void)
{
    g_gles2_rotmask_source_generation++;
    if( g_gles2_rotmask_source_generation == 0u )
        g_gles2_rotmask_source_generation = 1u; /* 0 is "never uploaded" in a slot */
}

uint32_t
gles2_rotmask_source_generation(void)
{
    return g_gles2_rotmask_source_generation;
}

struct ToriRS_GLES2*
ToriRS_GLES2_New(int width, int height)
{
    struct ToriRS_GLES2* renderer;
    static uint8_t white_tile[TRSPK_ATLAS_TILE * TRSPK_ATLAS_TILE * 4u];
    uint32_t group;
    int texture;

    assert(width > 0);
    assert(height > 0);
    renderer = (struct ToriRS_GLES2*)calloc(1u, sizeof(*renderer));
    assert(renderer);
    renderer->width = width;
    renderer->height = height;
    renderer->interface_scale_mode = 2;
    renderer->tex_slot_next = 1u;
    renderer->current_batch_slot = -1;
    /* TORIRS_GLES2_DEBUG=1: the 300-frame counters and the debug-only GL
     * error checks. Read once here; it used to be a getenv in gles2_end_3d. */
    renderer->debug = getenv("TORIRS_GLES2_DEBUG") != NULL;
    /* The levers (see the struct): each defaults ON; NAME=0 is the control
     * arm. Read once, here, so no frame ever scans the environment. */
    renderer->lever_ui_defer = gles2_lever_enabled("TORIRS_GLES2_UI_DEFER");
    renderer->lever_resident_fast = gles2_lever_enabled("TORIRS_GLES2_RESIDENT_FAST");
    renderer->lever_triplet_neon = gles2_lever_enabled("TORIRS_GLES2_TRIPLET_NEON");
    renderer->lever_rotmask_gen = gles2_lever_enabled("TORIRS_GLES2_ROTMASK_GEN");
    for( texture = 0; texture < TORIDRAW_TEXTURE_ID_CAPACITY; texture++ )
        renderer->tex_slot_of_id[texture] = -1;
    trspk_pose_table_init(&renderer->poses);
    trspk_pose_table_init(&renderer->batch_poses);
    renderer->frame_stream_cpu = trspk_vbo_create(0u, TRSPK_VERTEX_FORMAT_GLES2);
    assert(renderer->frame_stream_cpu);
    if( !trspk_atlas_init_grid(
            &renderer->atlas,
            GLES2_ATLAS_DIM,
            GLES2_ATLAS_DIM,
            TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE,
            4u) )
    {
        ToriRS_GLES2_Free(renderer);
        return NULL;
    }
    memset(white_tile, 0xff, sizeof(white_tile));
    if( !trspk_atlas_grid_insert_at(
            &renderer->atlas, 0u, white_tile, TRSPK_ATLAS_TILE * 4u, TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE, NULL) )
    {
        ToriRS_GLES2_Free(renderer);
        return NULL;
    }
    renderer->tex_resident[0] = 1u;
    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
    {
        struct GLES2ModelGroup* model_group = &renderer->groups[group];
        model_group->vbo_cpu = trspk_vbo_create(0u, TRSPK_VERTEX_FORMAT_GLES2);
        assert(model_group->vbo_cpu);
        model_group->arena = trspk_modelarena_create(
            model_group->vbo_cpu, &model_group->triangles, GLES2_VBO_PAGE, 64u);
        assert(model_group->arena);
        model_group->reset_each_frame = group == TRSPK_VBO_GROUP_DYNAMIC;
    }
    gles2_ui_init_state(renderer);
    return renderer;
}

static uint64_t
gles2_pose_table_bytes(const struct TRSPK_PoseTable* table)
{
    uint64_t bytes = (uint64_t)table->element_cap * sizeof(struct TRSPK_PoseElement);
    uint32_t element_index;
    uint32_t track;
    for( element_index = 0u; element_index < table->element_count; element_index++ )
        for( track = 0u; track < TRSPK_POSE_TRACK_COUNT; track++ )
            bytes += (uint64_t)table->elements[element_index].tracks[track].pose_cap *
                sizeof(uint32_t);
    return bytes;
}

/* One-shot shutdown attribution of every retained pool the renderer owns,
 * the peer of d3d9_report_retained_memory. GL buffer sizes are what was
 * asked for; the driver's own copy is not visible from here. */
static void
gles2_report_retained_memory(struct ToriRS_GLES2* renderer)
{
    uint64_t batch_vbo_cpu = 0u;
    uint64_t batch_tri_cpu = 0u;
    uint32_t batch_chunks = 0u;
    uint64_t group_vbo_cpu[TRSPK_VBO_GROUP_COUNT];
    uint64_t group_tri_cpu[TRSPK_VBO_GROUP_COUNT];
    uint64_t group_slots_cpu[TRSPK_VBO_GROUP_COUNT];
    uint64_t group_vbo_gpu[TRSPK_VBO_GROUP_COUNT];
    double pose_table_megabytes;
    uint32_t batch;
    uint32_t group;
    uint32_t chunk_index;

    assert(renderer);
    for( batch = 0u; batch < renderer->static_batch_count; batch++ )
    {
        struct TRSPK_Batch16* cpu = renderer->static_batches[batch].cpu;
        uint32_t count;
        if( !cpu )
            continue;
        count = trspk_batch16_chunk_count(cpu);
        for( chunk_index = 0u; chunk_index < count; chunk_index++ )
        {
            const struct TRSPK_Batch16Chunk* chunk = trspk_batch16_get_chunk(cpu, chunk_index);
            if( !chunk )
                continue;
            batch_chunks++;
            if( chunk->vbo )
                batch_vbo_cpu += (uint64_t)chunk->vbo->capacity * sizeof(struct TRSPK_VertexGLES2);
            batch_tri_cpu += (uint64_t)chunk->triangles.cap * sizeof(int);
        }
    }
    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
    {
        const struct GLES2ModelGroup* model_group = &renderer->groups[group];
        group_vbo_cpu[group] = model_group->vbo_cpu
            ? (uint64_t)model_group->vbo_cpu->capacity * sizeof(struct TRSPK_VertexGLES2)
            : 0u;
        group_tri_cpu[group] = (uint64_t)model_group->triangles.cap * sizeof(int);
        group_slots_cpu[group] = model_group->arena
            ? (uint64_t)model_group->arena->slot_capacity * sizeof(struct TRSPK_ModelSlot)
            : 0u;
        group_vbo_gpu[group] =
            (uint64_t)model_group->gpu_capacity * sizeof(struct TRSPK_VertexGLES2);
    }
    pose_table_megabytes = ((double)gles2_pose_table_bytes(&renderer->poses) +
                               (double)gles2_pose_table_bytes(&renderer->batch_poses)) /
        1048576.0;
    /* TORIRS_LOG compiles out of a release build; the figure is still computed
     * so the function that produces it is not dead code there. */
    (void)pose_table_megabytes;
    TORIRS_LOG("gles2_mem: === retained memory report ===\n"
               "gles2_mem: batch16_cpu_vertices  %10.2f MB (%u chunks)\n"
               "gles2_mem: batch16_cpu_configs   %10.2f MB\n"
               "gles2_mem: static_pages_gpu      %10.2f MB (%u pages)\n"
               "gles2_mem: group_static_cpu      %10.2f MB (vbo) + %.2f MB (configs) + %.2f MB (slots)\n"
               "gles2_mem: group_static_gpu      %10.2f MB\n"
               "gles2_mem: group_dynamic_cpu     %10.2f MB (vbo) + %.2f MB (configs)\n"
               "gles2_mem: group_dynamic_gpu     %10.2f MB\n"
               "gles2_mem: index_stream_gpu      %10.2f MB (one of %u)\n"
               "gles2_mem: frame_stream_gpu      %10.2f MB (one of %u) + %.2f MB (cpu)\n"
               "gles2_mem: draw_items_cpu        %10.2f MB\n"
               "gles2_mem: ibo_staging_cpu       %10.2f MB\n"
               "gles2_mem: model_indices_cpu     %10.2f MB\n"
               "gles2_mem: atlas_cpu             %10.2f MB world + %.2f MB ui\n"
               "gles2_mem: pose_tables_cpu       %10.2f MB\n",
        (double)batch_vbo_cpu / 1048576.0,
        batch_chunks,
        (double)batch_tri_cpu / 1048576.0,
        (double)renderer->static_batch_gpu_vertex_capacity * sizeof(struct TRSPK_VertexGLES2) /
            1048576.0,
        renderer->static_page_count,
        (double)group_vbo_cpu[TRSPK_VBO_GROUP_STATIC] / 1048576.0,
        (double)group_tri_cpu[TRSPK_VBO_GROUP_STATIC] / 1048576.0,
        (double)group_slots_cpu[TRSPK_VBO_GROUP_STATIC] / 1048576.0,
        (double)group_vbo_gpu[TRSPK_VBO_GROUP_STATIC] / 1048576.0,
        (double)group_vbo_cpu[TRSPK_VBO_GROUP_DYNAMIC] / 1048576.0,
        (double)group_tri_cpu[TRSPK_VBO_GROUP_DYNAMIC] / 1048576.0,
        (double)group_vbo_gpu[TRSPK_VBO_GROUP_DYNAMIC] / 1048576.0,
        (double)renderer->index_stream.capacities[renderer->frame_slot] / 1048576.0,
        GLES2_FRAMES_IN_FLIGHT,
        (double)renderer->frame_stream.capacities[renderer->frame_slot] / 1048576.0,
        GLES2_FRAMES_IN_FLIGHT,
        renderer->frame_stream_cpu
            ? (double)renderer->frame_stream_cpu->capacity * sizeof(struct TRSPK_VertexGLES2) /
                1048576.0
            : 0.0,
        (double)renderer->draw_item_capacity * sizeof(struct GLES2DrawItem) / 1048576.0,
        (double)renderer->ibo_staging_capacity * sizeof(uint16_t) / 1048576.0,
        (double)renderer->model_index_capacity * sizeof(uint16_t) / 1048576.0,
        (double)renderer->atlas.stride * renderer->atlas.height / 1048576.0,
        (double)renderer->ui_sprite_atlas.stride * renderer->ui_sprite_atlas.height / 1048576.0,
        pose_table_megabytes);
    gles2_ui_report_memory(renderer);
    gles2_zbuffer_report_memory(renderer);
}

static void
gles2_destroy_gl_resources(struct ToriRS_GLES2* renderer)
{
    uint32_t group;
    assert(renderer);
    gles2_ui_destroy_gl(renderer);
    gles2_delete_program(&renderer->program_world_plain);
    gles2_delete_program(&renderer->program_world_cutout);
    gles2_delete_program(&renderer->program_ui);
    gles2_delete_program(&renderer->program_rotmask);
    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
    {
        /* A per-frame group's buffers belong to the dynamic stream set. */
        if( !renderer->groups[group].reset_each_frame && renderer->groups[group].vbo_gpu )
            glDeleteBuffers(1, &renderer->groups[group].vbo_gpu);
        renderer->groups[group].vbo_gpu = 0u;
        renderer->groups[group].gpu_capacity = 0u;
    }
    if( renderer->static_batch_vbo )
        glDeleteBuffers(1, &renderer->static_batch_vbo);
    renderer->static_batch_vbo = 0u;
    renderer->static_batch_gpu_vertex_capacity = 0u;
    if( renderer->hot_vbo )
        glDeleteBuffers(1, &renderer->hot_vbo);
    renderer->hot_vbo = 0u;
    gles2_stream_set_destroy(&renderer->index_stream);
    gles2_stream_set_destroy(&renderer->dynamic_stream);
    gles2_stream_set_destroy(&renderer->frame_stream);
    gles2_stream_set_destroy(&renderer->ui_stream);
    renderer->ibo = 0u;
    renderer->frame_stream_vbo = 0u;
    renderer->ui_vbo = 0u;
    renderer->groups[TRSPK_VBO_GROUP_DYNAMIC].vbo_gpu = 0u;
    renderer->groups[TRSPK_VBO_GROUP_DYNAMIC].gpu_capacity = 0u;
    if( renderer->atlas_texture )
        glDeleteTextures(1, &renderer->atlas_texture);
    renderer->atlas_texture = 0u;
    renderer->atlas_texture_allocated = false;
}

void
ToriRS_GLES2_Free(struct ToriRS_GLES2* renderer)
{
    uint32_t batch;
    uint32_t group;
    if( !renderer )
        return;
    gles2_report_retained_memory(renderer);
    if( renderer->gl_context )
    {
        ToriRS_GLContext_MakeCurrent(renderer->window, renderer->gl_context);
        gles2_destroy_gl_resources(renderer);
    }
    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
    {
        if( renderer->groups[group].arena )
            trspk_modelarena_free(renderer->groups[group].arena);
        if( renderer->groups[group].vbo_cpu )
            trspk_vbo_free(renderer->groups[group].vbo_cpu);
        trspk_triangles_free(&renderer->groups[group].triangles);
    }
    free(renderer->hot_stage);
    if( renderer->frame_stream_cpu )
        trspk_vbo_free(renderer->frame_stream_cpu);
    trspk_triangles_free(&renderer->frame_stream_triangles);
    free(renderer->draw_items);
    trspk_pose_table_free(&renderer->poses);
    trspk_pose_table_free(&renderer->batch_poses);
    gles2_zbuffer_destroy(renderer);
    for( batch = 0u; batch < renderer->static_batch_count; batch++ )
    {
        trspk_batch16_destroy(renderer->static_batches[batch].cpu);
        free(renderer->static_batches[batch].page_ids);
        free(renderer->static_batches[batch].hot_serial);
    }
    if( trspk_atlas_is_initialized(&renderer->atlas) )
        trspk_atlas_free(&renderer->atlas);
    gles2_ui_free(renderer);
    free(renderer->upload_stage);
    free(renderer->ibo_staging);
    free(renderer->model_indices);
    free(renderer->static_pages);
    free(renderer->static_batches);
    if( renderer->gl_context )
        ToriRS_GLContext_Delete(renderer->gl_context);
    free(renderer);
}

bool
ToriRS_GLES2_Init(
    struct ToriRS_GLES2* renderer,
    ToriRS_GLWindow* window,
    struct ToriDraw_Scene* scene,
    bool z_buffer)
{
    GLint max_texture_size = 0;

    assert(renderer);
    assert(window);
    assert(scene);
    if( renderer->gl_context )
        return false;
    /* The one place the two world implementations are chosen between. */
    gles2_zbuffer_destroy(renderer);
    if( z_buffer && !gles2_zbuffer_create(renderer) )
        return false;
    renderer->scene = scene;
    renderer->kernel = ToriDraw_KernelGetGpu();
    renderer->window = window;

    /* Depth is a CREATION attribute -- part of the EGL config -- which is why
     * it is a parameter of the create call. 16 bits: the format every GLES2
     * device offers; EGL treats the request as a floor, so a device with more
     * may hand more back. */
    renderer->gl_context = ToriRS_GLContext_Create(window, z_buffer ? 16 : 0);
    if( !renderer->gl_context )
    {
        TORIRS_ERR("GLES2: context creation failed: %s\n", ToriRS_GLContext_LastError());
        return false;
    }
    ToriRS_GLContext_SetSwapInterval(0);

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    TORIRS_LOG("GLES2: %s | GLSL %s | %s | max texture %d\n",
        (const char*)glGetString(GL_VERSION),
        (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION),
        (const char*)glGetString(GL_RENDERER),
        (int)max_texture_size);
    if( max_texture_size < (GLint)GLES2_ATLAS_DIM )
    {
        TORIRS_ERR("GLES2: GL_MAX_TEXTURE_SIZE %d is below the %u atlas this renderer needs\n",
            (int)max_texture_size,
            GLES2_ATLAS_DIM);
        goto fail;
    }
    if( !gles2_create_programs(renderer) )
        goto fail;

    glGenTextures(1, &renderer->atlas_texture);
    if( !gles2_upload_atlas(renderer) )
        goto fail;
    if( !gles2_ui_create_gl(renderer) )
        goto fail;

    /* The first four attributes are live for the life of the context (the
     * fourth is the world's texinfo and the UI's sampler select; the rotmask
     * layout keeps it pointed at something valid); the mask uv follows the
     * rotmask layout. */
    glEnableVertexAttribArray(GLES2_ATTRIB_POSITION);
    glEnableVertexAttribArray(GLES2_ATTRIB_TEXCOORD);
    glEnableVertexAttribArray(GLES2_ATTRIB_COLOR);
    glEnableVertexAttribArray(GLES2_ATTRIB_TEXINFO);
    gles2_state_reset(renderer);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    if( !gles2_check_error("init") )
        goto fail;

    if( renderer->static_page_count > 0u )
    {
        bool recreated = false;
        if( !gles2_ensure_static_batch_vbo(renderer, renderer->static_page_count, &recreated) )
            goto fail;
        if( recreated )
            gles2_mark_active_static_batches_dirty(renderer);
        if( !gles2_upload_dirty_static_batches(renderer) )
            goto fail;
    }
    TORIRS_LOG("GLES2: renderer up (%s world pass)\n", z_buffer ? "depth-buffered" : "painter");
    return true;

fail:
    gles2_destroy_gl_resources(renderer);
    ToriRS_GLContext_Delete(renderer->gl_context);
    renderer->gl_context = NULL;
    return false;
}

void
ToriRS_GLES2_SetViewport(struct ToriRS_GLES2* renderer, int width, int height)
{
    assert(renderer);
    if( width <= 0 || height <= 0 || (renderer->width == width && renderer->height == height) )
        return;
    renderer->width = width;
    renderer->height = height;
    gles2_update_letterbox(renderer);
    renderer->in2d = false;
    gles2_ui_batch_reset(renderer);
}

void
ToriRS_GLES2_SetInterfaceScaleMode(struct ToriRS_GLES2* renderer, int mode)
{
    assert(renderer);
    mode = gles2_clampi(mode, 0, 2);
    if( renderer->interface_scale_mode == mode )
        return;
    renderer->interface_scale_mode = mode;
    renderer->ui_filter_dirty = true;
}

GLenum
gles2_ui_filter(const struct ToriRS_GLES2* renderer)
{
    assert(renderer);
    return renderer->interface_scale_mode == 0 ? GL_NEAREST : GL_LINEAR;
}

void
ToriRS_GLES2_SetPick(struct ToriRS_GLES2* renderer, int mouse_x, int mouse_y)
{
    assert(renderer);
    renderer->pick_enabled = true;
    renderer->pick_mouse_x = mouse_x;
    renderer->pick_mouse_y = mouse_y;
    ToriRS_PickHitsReset(&renderer->pick_hits);
}

struct ToriRS_PickHits const*
ToriRS_GLES2_PickHits(struct ToriRS_GLES2 const* renderer)
{
    assert(renderer);
    return &renderer->pick_hits;
}

void
ToriRS_GLES2_Execute(struct ToriRS_GLES2* renderer, struct ToriRS_RenderCommand const* command)
{
    gles2_dispatch(renderer, command);
}

/* Bring the surface up for a frame: current, measured, letterboxed, cleared.
 * False when there is no surface to draw on (a stopped activity). */
static bool
gles2_begin_frame(struct ToriRS_GLES2* renderer, bool clear_to_black_only)
{
    struct GLES2Rect letterbox;
    assert(renderer);
    if( !renderer->gl_context )
        return false;
    if( ToriRS_GLContext_MakeCurrent(renderer->window, renderer->gl_context) != 0 )
        return false;
    ToriRS_GLContext_DrawableSize(
        renderer->window, &renderer->drawable_width, &renderer->drawable_height);
    if( renderer->drawable_width <= 0 || renderer->drawable_height <= 0 )
        return false;
    gles2_update_letterbox(renderer);
    gles2_state_reset(renderer);
    gles2_stream_sets_begin_frame(renderer);
    glViewport(0, 0, renderer->drawable_width, renderer->drawable_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if( !clear_to_black_only && renderer->letterbox_width > 0 && renderer->letterbox_height > 0 )
    {
        letterbox.x = renderer->letterbox_x;
        letterbox.y = renderer->letterbox_y;
        letterbox.width = renderer->letterbox_width;
        letterbox.height = renderer->letterbox_height;
        gles2_set_scissor(renderer, &letterbox);
        glClearColor(
            (float)((TORIRS_GLES2_BG >> 16) & 0xffu) / 255.0f,
            (float)((TORIRS_GLES2_BG >> 8) & 0xffu) / 255.0f,
            (float)(TORIRS_GLES2_BG & 0xffu) / 255.0f,
            1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        gles2_set_scissor(renderer, NULL);
    }
    gles2_set_letterbox_viewport(renderer);
    return true;
}

/* The bar's caption, through the same font path a frame uses, so a boot
 * sentence is one picture and not one per renderer. */
static void
gles2_draw_boot_caption(struct ToriRS_GLES2* renderer, int caption_font_id, char const* caption)
{
    struct ToriRS_RenderCommand_Font font_command;
    assert(renderer);
    assert(caption);
    assert(caption_font_id >= 0);
    memset(&font_command, 0, sizeof(font_command));
    font_command.font_id = caption_font_id;
    font_command.x = BootBar_OriginX(renderer->width) + BOOT_BAR_W / 2;
    font_command.y = BootBar_OriginY(renderer->height) + BOOT_BAR_TEXT_BASELINE;
    font_command.color = 0xFFFFFF;
    font_command.center = 1;
    font_command.baseline = 1;
    font_command.text = caption;
    font_command.scissor_w = renderer->width;
    font_command.scissor_h = renderer->height;
    gles2_begin_2d(renderer);
    gles2_ui_draw_font(renderer, &font_command);
    gles2_end_2d(renderer);
}

void
ToriRS_GLES2_DrawBootBar(
    struct ToriRS_GLES2* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    assert(renderer);
    /* progress < 0: clear only, no bar -- the post-login loading screen,
     * which is a black screen and the sentence alone on every lane. */
    if( !gles2_begin_frame(renderer, progress < 0) )
        return;
    if( progress >= 0 )
    {
        int bar_x;
        int bar_y;
        int fill_w;
        progress = gles2_clampi(progress, 0, 100);
        /* The references' bar, not one of ours (engine/boot_bar.h): a filled
         * red track, a black inset one pixel in, then the fill two pixels in. */
        bar_x = renderer->width / 2 - BOOT_BAR_W / 2;
        bar_y = renderer->height / 2 - BOOT_BAR_ABOVE_CENTRE;
        fill_w = progress * BOOT_BAR_PX_PER_PERCENT;
        gles2_draw_solid_rect(
            renderer, bar_x, bar_y, BOOT_BAR_W, BOOT_BAR_H, 0xff000000u | BOOT_BAR_COLOR);
        gles2_draw_solid_rect(
            renderer, bar_x + 1, bar_y + 1, BOOT_BAR_W - 2, BOOT_BAR_H - 2, 0xff000000u);
        if( fill_w > 0 )
            gles2_draw_solid_rect(
                renderer,
                bar_x + BOOT_BAR_INSET,
                bar_y + BOOT_BAR_INSET,
                fill_w,
                BOOT_BAR_FILL_H,
                0xff000000u | BOOT_BAR_COLOR);
    }
    if( caption && caption[0] && caption_font_id >= 0 )
        gles2_draw_boot_caption(renderer, caption_font_id, caption);
}

bool
gles2_render_frame_begin(struct ToriRS_GLES2* renderer)
{
    assert(renderer);
    if( !gles2_begin_frame(renderer, false) )
        return false;
    renderer->has_3d = false;
    renderer->in3d = false;
    renderer->in2d = false;
    renderer->frame_clock += 1.0;
    return true;
}

void
gles2_render_frame_commands(struct ToriRS_GLES2* renderer, struct ToriRS_Frame* frame)
{
    struct ToriRS_RenderCommand command;
    assert(renderer);
    assert(frame);
    while( ToriRS_FrameNextCommand(frame, &command) )
    {
        gles2_prefetch_ahead(renderer, frame);
        gles2_dispatch(renderer, &command);
    }
}

void
ToriRS_GLES2_RenderFrame(struct ToriRS_GLES2* renderer, struct ToriRS_Frame* frame)
{
    assert(renderer);
    assert(frame);
    if( !gles2_render_frame_begin(renderer) )
        return;
    ToriRS_FrameBegin(frame);
    gles2_render_frame_commands(renderer, frame);
    ToriRS_FrameEnd(frame);
    gles2_render_frame_end(renderer);
}

void
gles2_render_frame_end(struct ToriRS_GLES2* renderer)
{
    assert(renderer);
    if( renderer->in3d )
        gles2_end_3d(renderer);
    if( renderer->in2d )
        gles2_end_2d(renderer);

    /* TORIRS_GLES2_READBACK=path dumps one frame (after
     * TORIRS_GLES2_READBACK_FRAME, default 90) through the same readback the
     * app's screenshots use, so a bug in the letterbox arithmetic cannot show
     * in a debug dump and not in a screenshot. */
    {
        /* Read once: getenv is a linear scan of the environment, and this
         * ran twice per frame on a path that is dormant in every ordinary
         * session. */
        static char const* path = NULL;
        static long want = 90;
        static int probed = 0;
        static int done = 0;
        if( !probed )
        {
            char const* frame = getenv("TORIRS_GLES2_READBACK_FRAME");
            path = getenv("TORIRS_GLES2_READBACK");
            if( frame )
                want = atol(frame);
            probed = 1;
        }
        if( path && path[0] && !done && renderer->frame_clock >= (double)want )
        {
            int* top = (int*)malloc((size_t)renderer->width * (size_t)renderer->height * sizeof(int));
            void bmp_write_file(const char* filename, int* px, int w, int h);
            done = 1;
            assert(top);
            if( ToriRS_GLES2_ReadPixels(renderer, top, renderer->width, renderer->height) )
            {
                bmp_write_file(path, top, renderer->width, renderer->height);
                TORIRS_LOG("gles2_readback: wrote %s\n", path);
            }
            free(top);
        }
    }
}

/*
 * The frame that is about to be presented, sampled back onto the canvas grid.
 *
 * Two conversions: the drawable is letterboxed, and GL reports rows bottom-up
 * while the client's buffers are top-down. GLES2 reads GL_RGBA only, so the
 * bytes are repacked into the ARGB words the rest of the client thinks in.
 */
bool
ToriRS_GLES2_ReadPixels(struct ToriRS_GLES2* renderer, int* pixels, int width, int height)
{
    int framebuffer_w = 0;
    int framebuffer_h = 0;
    uint8_t* framebuffer;
    float scale_x;
    float scale_y;
    int y;

    assert(renderer);
    assert(pixels);
    assert(width > 0);
    assert(height > 0);
    if( !renderer->gl_context || !renderer->window )
        return false;
    ToriRS_GLContext_DrawableSize(renderer->window, &framebuffer_w, &framebuffer_h);
    if( framebuffer_w <= 0 || framebuffer_h <= 0 || renderer->letterbox_width <= 0 ||
        renderer->letterbox_height <= 0 )
        return false;
    framebuffer = (uint8_t*)malloc((size_t)framebuffer_w * (size_t)framebuffer_h * 4u);
    assert(framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, framebuffer_w, framebuffer_h, GL_RGBA, GL_UNSIGNED_BYTE, framebuffer);
    scale_x = (float)renderer->letterbox_width / (float)width;
    scale_y = (float)renderer->letterbox_height / (float)height;
    for( y = 0; y < height; y++ )
    {
        int source_y = renderer->letterbox_y + (int)((float)(height - 1 - y) * scale_y);
        int x;
        source_y = gles2_clampi(source_y, 0, framebuffer_h - 1);
        for( x = 0; x < width; x++ )
        {
            int source_x = renderer->letterbox_x + (int)((float)x * scale_x);
            const uint8_t* source;
            source_x = gles2_clampi(source_x, 0, framebuffer_w - 1);
            source = framebuffer + ((size_t)source_y * (size_t)framebuffer_w + (size_t)source_x) * 4u;
            pixels[y * width + x] = (int)(((uint32_t)source[3] << 24) | ((uint32_t)source[0] << 16) |
                                          ((uint32_t)source[1] << 8) | (uint32_t)source[2]);
        }
    }
    free(framebuffer);
    return true;
}
