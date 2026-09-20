/**
 * The WebGL2 renderer core: the context, the programs, the world texture atlas,
 * every retained CPU/GPU vertex buffer, the per-frame index stream and the
 * command dispatch.
 *
 * What it deliberately does not know is how the world's triangles get ordered.
 * It does not know it in the strong sense: there is no test anywhere in this
 * file for which of the two world paths is running. Each is a WHOLE renderer
 * that composes the toolkit below, and they share nothing with each other:
 *
 *   platform_androidarmv7_renderer_opengles3_painter.c   painter's algorithm
 *   platform_androidarmv7_renderer_opengles3_zbuffer.c   hardware depth test
 *
 * The caller picks one by calling its Init, and keeps calling that one's
 * Execute, DrawBootBar and RenderFrame. ::zbuffer is the depth renderer's
 * own state, not a selector anything reads.
 * See 3rd/trspk/es3/es3_core.h for the contract and for what the
 * WebGL2 ceiling turned into here.
 *
 * The retained model is the D3D9 renderer's, kept on purpose (see
 * platform_win32_renderer_d3d9_core.c): two arena groups (STATIC, retained
 * across frames; DYNAMIC, refilled every frame for actors), plus Batch16 for
 * the scene build, whose pages are the pages the U16 index stream addresses.
 */

#include "es3/es3_core.h"

#include "core/trspk_math.h"
#include "engine/boot_bar.h"
#include "log/torirs_log.h"
#include "painters/painters.h"
#include "perf/torirs_perf.h"
#include "platform/platform_androidarmv7_renderer_opengles3_placement.h"
#include "es3/es3_shaders.h"
#include "toridraw.h"
#include "toridraw_element_id.h"
#include "toridraw_math.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- this family's specialisations ------------------------------------------------ */

/*
 * ANDROID armv7. These are the two places the opengles3 renderer diverges from
 * the webgl2 one, and they are written out here rather than selected, because
 * this file is only ever compiled for the device that wants them.
 */

void
es3_apply_animation(struct TRSPK_Renderer_ES3* renderer, const struct ToriRS_RenderCommand_Model* command)
{
    assert(renderer);
    assert(command);
    /*
     * Resolved, so an element whose pose did not move is not re-posed. The
     * phone is CPU-bound and ToriDraw_AnimApplyTransform was 1.71% of its
     * frame against the opengles2 renderer's 0.18% -- ten times -- purely
     * because this renderer used to take the other arm.
     */
    ToriDraw_SceneElementApplyAnimationResolved(
        ToriDraw_SceneElementGet(renderer->scene, command->element_id),
        command->element_id,
        command->anim_index == 0,
        command->anim_frame,
        true);
}

/** @return true when the packed encoder wrote the whole run. */
static bool
es3_bake_ordered_fast(
    struct TRSPK_VBO* vbo,
    uint32_t vertex_base,
    const struct ToriDraw_Model* full_model,
    const int* face_order,
    uint32_t written_count,
    const float* world_xyz,
    bool ordered_painter)
{
    assert(vbo);
    assert(full_model);
    /* The preconditions are the ENCODER's: it writes a packed untextured
     * vertex in painter order, so a textured model or an unordered run still
     * takes the generic loop below. */
    if( !ordered_painter || !world_xyz || full_model->face_textures )
        return false;
    trspk_toridraw_gles2_untextured_words(
        full_model, face_order, written_count, world_xyz, &vbo->vertices.as_gles2[vertex_base]);
    trspk_vbo_mark_dirty_range(vbo, vertex_base, written_count * 3u);
    return true;
}


#if defined(TORIRS_BAKE_CHAIN_CAPTURE)
#include "es3/es3_bake_capture.u.c"
#elif defined(TORIRS_BAKE_VERIFY)
#include "../../tools/perf/bake_chain_format.h"
#endif

#if defined(TORIRS_PLACEMENT_CAPTURE)
#include "es3/es3_placement_capture.u.c"
#endif


#if defined(TORIRS_MODEL_CHAIN_CAPTURE)
#include "es3/es3_chain_capture.u.c"
#endif

_Static_assert(
    ES3_ATLAS_COLS* TRSPK_ATLAS_TILE == ES3_ATLAS_DIM,
    "the atlas grid must tile the atlas exactly");
_Static_assert(
    ES3_ATLAS_COLS* ES3_ATLAS_COLS == ES3_ATLAS_SLOTS,
    "the slot count is the grid squared");
_Static_assert(
    sizeof(struct TRSPK_VertexGLES2) == 28u,
    "the world vertex layout is what the attribute pointers describe");
_Static_assert(
    sizeof(struct ES3VertexUI) == 28u,
    "the UI vertex layout is what the attribute pointers describe");
_Static_assert(
    sizeof(struct ES3VertexRotmask) == 32u,
    "the rotmask vertex layout is what the attribute pointers describe");
_Static_assert(
    ES3_ATTRIB_TEXINFO == ES3_ATTRIB_MASK_TEXCOORD,
    "the fourth attribute slot is shared: world texinfo or rotmask mask uv");

enum ES3StreamLayout
{
    ES3_STREAM_NONE = 0,
    ES3_STREAM_WORLD = 1,
    ES3_STREAM_UI = 2,
    ES3_STREAM_ROTMASK = 3,
};

#define ES3_VERTEX_STRIDE ((GLsizei)sizeof(struct TRSPK_VertexGLES2))

/* ---- cached GL state ------------------------------------------------------ */

void
es3_set_blend(
    struct TRSPK_Renderer_ES3* renderer,
    bool enabled)
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
es3_set_depth(
    struct TRSPK_Renderer_ES3* renderer,
    bool test,
    bool write)
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
es3_set_cull(
    struct TRSPK_Renderer_ES3* renderer,
    bool enabled)
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
es3_set_scissor(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ES3Rect* rect)
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
es3_bind_texture0(
    struct TRSPK_Renderer_ES3* renderer,
    GLuint texture)
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
 * batch entry read alone was 39% of es3_dispatch.
 */
void
es3_prefetch_ahead_ids(
    struct TRSPK_Renderer_ES3* renderer,
    int id_plus1,
    int id_plus2,
    int id_plus3)
{
#if defined(TORIRS_PLACEMENT_CAPTURE)
    es3_placement_prefetch_record(renderer, id_plus1, id_plus2, id_plus3);
#endif
    es3_static_prefetch_ids(renderer, id_plus1, id_plus2, id_plus3);
}

void
es3_prefetch_ahead(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_Frame* frame)
{
    assert(renderer);
    assert(frame);
    if( !renderer->batch_poses.elements || !renderer->has_3d )
        return;
    es3_prefetch_ahead_ids(
        renderer,
        ToriRS_FrameLookaheadElementId(frame, 1),
        ToriRS_FrameLookaheadElementId(frame, 2),
        ToriRS_FrameLookaheadElementId(frame, 3));
}

void
es3_bind_texture1(
    struct TRSPK_Renderer_ES3* renderer,
    GLuint texture)
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
es3_use_program(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ES3Program* program)
{
    assert(renderer);
    assert(program);
    if( renderer->current_program == program )
        return;
    glUseProgram(program->id);
    renderer->current_program = program;
}

void
es3_bind_array_buffer(
    struct TRSPK_Renderer_ES3* renderer,
    GLuint buffer)
{
    if( renderer->bound_array_buffer == buffer )
        return;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    renderer->bound_array_buffer = buffer;
}

/* Put GL into a known state and make the cache agree with it. Every frame
 * starts here: the context is shared with nothing, but the cost is a dozen
 * calls and it makes a stale cache impossible rather than unlikely. */
void
es3_blend_func_default(void)
{
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void
es3_state_reset(struct TRSPK_Renderer_ES3* renderer)
{
    glDisable(GL_BLEND);
    es3_blend_func_default();
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
    glBindVertexArray(0);
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
    renderer->vao_bound = 0u;
    renderer->current_program = NULL;
    renderer->stream_buffer = 0u;
    renderer->stream_byte_offset = 0u;
    renderer->stream_layout = ES3_STREAM_NONE;
}

/* ---- programs --------------------------------------------------------------- */

/*
 * What this renderer calls itself, for every line the core logs.
 *
 * "WebGL2" in a browser, "GLES3" on a phone -- the lane decides, because the
 * core is neither. A file static for the same reason the ES2 core's is: the
 * places that log a shader or link failure have no renderer in scope, main.c
 * starts at most one GPU renderer at a time, and the value is the lane's,
 * fixed for the build. TRSPK_Renderer_ES3_New sets it.
 */
static char const* g_es3_name = "ES3";

char const*
es3_log_name(void)
{
    return g_es3_name;
}

bool
es3_check_error(const char* where)
{
    GLenum error = glGetError();
    if( error == GL_NO_ERROR )
        return true;
    TORIRS_ERR("%s: %s: glGetError 0x%x\n", g_es3_name, where, (unsigned)error);
    return false;
}

static GLuint
es3_compile_shader(
    GLenum type,
    const char* source)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;
    if( shader == 0u )
    {
        TORIRS_ERR("%s: glCreateShader failed\n", g_es3_name);
        return 0u;
    }
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char log[1024];
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        TORIRS_ERR("%s: shader compile failed: %s\n", g_es3_name, log);
        glDeleteShader(shader);
        return 0u;
    }
    return shader;
}

static bool
es3_link_program(
    struct ES3Program* program,
    const char* vertex_source,
    const char* fragment_source,
    const char* label)
{
    GLuint vertex_shader = es3_compile_shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment_shader = es3_compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    GLint ok = 0;

    memset(program, 0, sizeof(*program));
    if( vertex_shader == 0u || fragment_shader == 0u )
    {
        if( vertex_shader )
            glDeleteShader(vertex_shader);
        if( fragment_shader )
            glDeleteShader(fragment_shader);
        TORIRS_ERR("%s: %s: shaders did not compile\n", g_es3_name, label);
        return false;
    }
    program->id = glCreateProgram();
    if( program->id == 0u )
    {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        TORIRS_ERR("%s: %s: glCreateProgram failed\n", g_es3_name, label);
        return false;
    }
    glAttachShader(program->id, vertex_shader);
    glAttachShader(program->id, fragment_shader);
    /* No glBindAttribLocation: every shader here declares its own
     * `layout(location = N) in`, which is the ES 3.00 way and removes the
     * chance of the C side and the shader disagreeing about a slot. The
     * two flags the ES2 renderer needed for this are gone with it. */
    glLinkProgram(program->id);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    glGetProgramiv(program->id, GL_LINK_STATUS, &ok);
    if( !ok )
    {
        char log[1024];
        glGetProgramInfoLog(program->id, (GLsizei)sizeof(log), NULL, log);
        TORIRS_ERR("%s: %s: link failed: %s\n", g_es3_name, label, log);
        glDeleteProgram(program->id);
        program->id = 0u;
        return false;
    }
    program->u_matrix = glGetUniformLocation(program->id, "u_matrix");
    program->u_texture = glGetUniformLocation(program->id, "s_texture");
    program->u_mask = glGetUniformLocation(program->id, "s_mask");
    program->u_mask_invert = glGetUniformLocation(program->id, "u_mask_invert");
    /* A world program reads its matrix and clock from the shared block
     * rather than from uniforms of its own; point its block at the binding
     * the pass uploads to. The 2D programs have no block and skip this. */
    program->world_block = glGetUniformBlockIndex(program->id, "WorldBlock");
    if( program->world_block != GL_INVALID_INDEX )
        glUniformBlockBinding(program->id, program->world_block, ES3_WORLD_BLOCK_BINDING);
    /* Sampler bindings never change: unit 0 is the texture, unit 1 the mask. */
    glUseProgram(program->id);
    if( program->u_texture >= 0 )
        glUniform1i(program->u_texture, 0);
    if( program->u_mask >= 0 )
        glUniform1i(program->u_mask, 1);
    glUseProgram(0);
    return es3_check_error(label);
}

static void
es3_delete_program(struct ES3Program* program)
{
    if( program->id )
        glDeleteProgram(program->id);
    memset(program, 0, sizeof(*program));
}

/* The interface layer's composite: the present's vertex shader, and the
 * two uniforms ES3Program has no field for. */
static bool
es3_link_ui_composite_program(struct TRSPK_Renderer_ES3* renderer)
{
    if( !es3_link_program(
            &renderer->program_ui_composite,
            es3_present_vertex_shader,
            es3_ui_composite_fragment_shader,
            "ui composite") )
        return false;
    renderer->ui_composite_u_size =
        glGetUniformLocation(renderer->program_ui_composite.id, "u_size");
    renderer->ui_composite_u_filter =
        glGetUniformLocation(renderer->program_ui_composite.id, "u_filter");
    return true;
}

static bool
es3_create_programs(struct TRSPK_Renderer_ES3* renderer)
{
    /* TORIRS_ES3_FAST_SHADER=0 selects the plain sampling shader as the
     * A/B control arm. The fast one is the default here (the GLES2 renderer
     * turns it on only for the Adreno 320 it was measured on): it returns
     * the interpolated colour without a texture fetch for a face whose tile
     * is slot 0, which is every untextured face -- most of the terrain --
     * and a dependent texture fetch is the one thing a browser's compiled
     * shader cannot make cheaper. */

    /* Fresh program objects hold no uniform values yet. */
    renderer->ui_projection_pushed = false;
    renderer->rotmask_projection_pushed = false;
    return es3_link_program(
               &renderer->program_world_plain,
               es3_world_vertex_shader,
               es3_world_plain_fragment_shader,
               "world (plain)") &&
           es3_link_program(
               &renderer->program_world_cutout,
               es3_world_vertex_shader,
               es3_world_cutout_fragment_shader,
               "world (cutout)") &&
           es3_link_program(
               &renderer->program_world_fast_plain,
               es3_world_vertex_shader,
               es3_world_fast_plain_fragment_shader,
               "world fast plain") &&
           es3_link_program(
               &renderer->program_world_fast_cutout,
               es3_world_vertex_shader,
               es3_world_fast_cutout_fragment_shader,
               "world fast cutout") &&
           es3_link_program(
               &renderer->program_ui,
               es3_ui_vertex_shader,
               es3_ui_fragment_shader,
               "ui") &&
           es3_link_program(
               &renderer->program_rotmask,
               es3_rotmask_vertex_shader,
               es3_rotmask_fragment_shader,
               "rotmask") &&
           es3_link_program(
               &renderer->program_present,
               es3_present_vertex_shader,
               es3_present_fragment_shader,
               "present") &&
           es3_link_ui_composite_program(renderer);
}

/* ---- letterbox and rectangles ---------------------------------------------- */

/* The output rect, the render target and the letterbox inside it.
 * `allow_offscreen` false draws direct even when the render size differs
 * (the boot bar, which has no frame end to present from). */
void
es3_update_letterbox(
    struct TRSPK_Renderer_ES3* renderer,
    bool allow_offscreen)
{
    struct ClientScalePresent present;
    if( renderer->width <= 0 || renderer->height <= 0 || renderer->drawable_width <= 0 ||
        renderer->drawable_height <= 0 )
    {
        renderer->letterbox_x = 0;
        renderer->letterbox_y = 0;
        renderer->letterbox_top = 0;
        renderer->letterbox_width = 0;
        renderer->letterbox_height = 0;
        renderer->output_x = 0;
        renderer->output_y = 0;
        renderer->output_width = 0;
        renderer->output_height = 0;
        renderer->target_width = renderer->drawable_width;
        renderer->target_height = renderer->drawable_height;
        renderer->target_offscreen = false;
        return;
    }
    ClientScale_Present(
        &renderer->client_scale,
        renderer->width,
        renderer->height,
        renderer->drawable_width,
        renderer->drawable_height,
        &present);
    renderer->output_x = present.output.x;
    /* Top-left origin -> GL's bottom-left. */
    renderer->output_y = renderer->drawable_height - present.output.y - present.output.h;
    renderer->output_width = present.output.w;
    renderer->output_height = present.output.h;
    renderer->target_offscreen = allow_offscreen && (present.render_w != present.output.w ||
                                                     present.render_h != present.output.h);
    if( renderer->target_offscreen )
    {
        renderer->target_width = present.render_w;
        renderer->target_height = present.render_h;
        renderer->letterbox_x = 0;
        renderer->letterbox_y = 0;
        renderer->letterbox_top = 0;
        renderer->letterbox_width = present.render_w;
        renderer->letterbox_height = present.render_h;
    }
    else
    {
        renderer->target_width = renderer->drawable_width;
        renderer->target_height = renderer->drawable_height;
        renderer->letterbox_x = renderer->output_x;
        renderer->letterbox_y = renderer->output_y;
        renderer->letterbox_top = present.output.y;
        renderer->letterbox_width = renderer->output_width;
        renderer->letterbox_height = renderer->output_height;
    }
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
es3_scissor_rect(
    const struct TRSPK_Renderer_ES3* renderer,
    int logical_x,
    int logical_y,
    int logical_width,
    int logical_height,
    struct ES3Rect* out)
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
        renderer->height <= 0 || renderer->letterbox_width <= 0 || renderer->letterbox_height <= 0 )
        return false;
    x0 = es3_clampi(logical_x, 0, renderer->width);
    y0 = es3_clampi(logical_y, 0, renderer->height);
    x1 = es3_clampi(logical_x + logical_width, 0, renderer->width);
    y1 = es3_clampi(logical_y + logical_height, 0, renderer->height);
    if( x1 <= x0 || y1 <= y0 )
        return false;
    left = renderer->letterbox_x + (int)((int64_t)x0 * renderer->letterbox_width / renderer->width);
    top = renderer->letterbox_top +
          (int)((int64_t)y0 * renderer->letterbox_height / renderer->height);
    right =
        renderer->letterbox_x +
        (int)(((int64_t)x1 * renderer->letterbox_width + renderer->width - 1) / renderer->width);
    bottom =
        renderer->letterbox_top +
        (int)(((int64_t)y1 * renderer->letterbox_height + renderer->height - 1) / renderer->height);
    left = es3_clampi(left, 0, renderer->target_width);
    right = es3_clampi(right, left, renderer->target_width);
    top = es3_clampi(top, 0, renderer->target_height);
    bottom = es3_clampi(bottom, top, renderer->target_height);
    if( right <= left || bottom <= top )
        return false;
    out->x = left;
    out->width = right - left;
    /* GL's origin is the bottom-left corner of the target. */
    out->y = renderer->target_height - bottom;
    out->height = bottom - top;
    return true;
}

/* ---- the world texture atlas ---------------------------------------------- */

static void
es3_decode_texture_rgba(
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
es3_reserve_upload_stage(
    struct TRSPK_Renderer_ES3* renderer,
    size_t needed)
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
 * Only the merged dirty rectangle goes up, and it goes up IN PLACE:
 * GL_UNPACK_ROW_LENGTH tells GL how wide the source buffer really is, so a
 * sub-rectangle of the CPU atlas is handed to glTexSubImage2D where it lies.
 * The ES2 renderer has to pack those rows into a tight staging buffer first
 * -- a full copy of every dirty rectangle, every time one moves -- because
 * GL_UNPACK_ROW_LENGTH is GLES3 and above. The first upload allocates the
 * texture from the whole CPU atlas, with a sized internal format.
 */
static bool
es3_upload_atlas_texture(
    struct TRSPK_Renderer_ES3* renderer,
    struct TRSPK_Atlas* atlas,
    GLuint texture,
    bool* allocated,
    GLenum filter,
    int64_t* out_bytes)
{
    struct TRSPK_AtlasDirtyRect dirty;

    assert(renderer);
    assert(atlas);
    assert(allocated);
    assert(out_bytes);
    *out_bytes = 0;
    if( !trspk_atlas_is_initialized(atlas) || !atlas->pixels || texture == 0u )
        return false;
    es3_bind_texture0(renderer, texture);
    if( !*allocated )
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
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
    /* The atlas stride is in bytes and always a whole number of RGBA texels
     * (trspk_atlas allocates width * 4), which is what GL_UNPACK_ROW_LENGTH
     * counts in. */
    assert(atlas->stride % 4u == 0u);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(atlas->stride / 4u));
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        (GLint)dirty.x,
        (GLint)dirty.y,
        (GLsizei)dirty.w,
        (GLsizei)dirty.h,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        atlas->pixels + (size_t)dirty.y * atlas->stride + (size_t)dirty.x * 4u);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    *out_bytes = (int64_t)dirty.w * (int64_t)dirty.h * 4;
    trspk_atlas_clear_dirty(atlas);
    return true;
}

bool
es3_upload_atlas(struct TRSPK_Renderer_ES3* renderer)
{
    int64_t bytes = 0;
    assert(renderer);
    if( !renderer->gl_context )
        return false;
    if( !trspk_atlas_is_dirty(&renderer->atlas) && renderer->atlas_texture_allocated )
        return true;
    if( !es3_upload_atlas_texture(
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

/** The UI atlas upload lives with the UI, but shares the in-place path. */
bool
es3_upload_ui_atlas_texture(
    struct TRSPK_Renderer_ES3* renderer,
    int64_t* out_bytes);
bool
es3_upload_ui_atlas_texture(
    struct TRSPK_Renderer_ES3* renderer,
    int64_t* out_bytes)
{
    assert(renderer);
    return es3_upload_atlas_texture(
        renderer,
        &renderer->ui_sprite_atlas,
        renderer->ui_sprite_atlas_texture,
        &renderer->ui_sprite_atlas_allocated,
        GL_NEAREST,
        out_bytes);
}

int
es3_texture_slot(
    struct TRSPK_Renderer_ES3* renderer,
    int tex_id)
{
    int slot;
    assert(renderer);
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return -1;
    slot = renderer->tex_slot_of_id[tex_id];
    if( slot >= 0 )
        return slot;
    if( renderer->tex_slot_next >= ES3_ATLAS_SLOTS )
    {
        static bool warned;
        if( !warned )
        {
            TORIRS_LOG("%s: the 2048x2048 world texture atlas is full\n", g_es3_name);
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
es3_texture_anim_bytes(
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
    speed = es3_clampi(texture->animation_speed, -127, 127);
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
es3_scene_texture(
    struct TRSPK_Renderer_ES3* renderer,
    int tex_id)
{
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY || !renderer->scene )
        return NULL;
    return ToriDraw_TextureMapGet(&ToriDraw_SceneTexState(renderer->scene)->texture_map, tex_id);
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
es3_refresh_anim_range(
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
    if( vbo->format != TRSPK_VERTEX_FORMAT_GLES2 || !vbo->vertices.as_gles2 || !triangles->config ||
        vertex_count % 3u != 0u || vertex_base > vbo->vertex_count ||
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
es3_refresh_texture_animation(
    struct TRSPK_Renderer_ES3* renderer,
    int tex_id)
{
    const struct ToriDraw_Texture* texture = es3_scene_texture(renderer, tex_id);
    uint8_t anim_u;
    uint8_t anim_v;
    uint32_t group_index;
    uint32_t batch_slot;

    assert(renderer);
    es3_texture_anim_bytes(texture, &anim_u, &anim_v);
    for( group_index = 0u; group_index < TRSPK_VBO_GROUP_COUNT; group_index++ )
    {
        struct ES3ModelGroup* group = &renderer->groups[group_index];
        uint32_t slot_index;
        if( !group->arena || !group->vbo_cpu )
            continue;
        for( slot_index = 0u; slot_index < group->arena->slot_count; slot_index++ )
        {
            const struct TRSPK_ModelSlot* model_slot = &group->arena->slots[slot_index];
            if( !trspk_modelslot_is_alive(model_slot) )
                continue;
            (void)es3_refresh_anim_range(
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
        struct ES3StaticBatch* batch = &renderer->static_batches[batch_slot];
        uint32_t chunk_count;
        uint32_t chunk_index;
        if( !batch->cpu || (!batch->active && !batch->building) )
            continue;
        chunk_count = trspk_batch16_chunk_count(batch->cpu);
        for( chunk_index = 0u; chunk_index < chunk_count; chunk_index++ )
        {
            struct TRSPK_Batch16Chunk* chunk = trspk_batch16_get_chunk(batch->cpu, chunk_index);
            if( chunk && chunk->vbo &&
                es3_refresh_anim_range(
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

bool
es3_load_texture_object(
    struct TRSPK_Renderer_ES3* renderer,
    int tex_id,
    const struct ToriDraw_Texture* texture)
{
    static uint8_t rgba[TRSPK_ATLAS_TILE * TRSPK_ATLAS_TILE * 4u];
    int slot;
    assert(renderer);
    assert(texture);
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY || !texture->texels )
        return false;
    slot = es3_texture_slot(renderer, tex_id);
    if( slot < 0 )
        return false;
    es3_decode_texture_rgba(texture, TRSPK_ATLAS_TILE, rgba);
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
        es3_refresh_texture_animation(renderer, tex_id);
    return true;
}

/** Reserve the slot and, when the scene already holds the texels, upload
 *  them. The slot is what a bake encodes, resident or not. */
int
es3_ensure_texture(
    struct TRSPK_Renderer_ES3* renderer,
    int tex_id)
{
    struct ToriDraw_Texture* texture;
    int slot;
    assert(renderer);
    if( tex_id < 0 )
        return -1;
    slot = es3_texture_slot(renderer, tex_id);
    if( slot < 0 )
        return -1;
    if( renderer->tex_resident[slot] )
        return slot;
    texture = es3_scene_texture(renderer, tex_id);
    if( texture )
        (void)es3_load_texture_object(renderer, tex_id, texture);
    return slot;
}

void
es3_unload_texture(
    struct TRSPK_Renderer_ES3* renderer,
    int tex_id)
{
    int slot;
    assert(renderer);
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return;
    slot = renderer->tex_slot_of_id[tex_id];
    if( slot >= 0 && (uint32_t)slot < ES3_ATLAS_SLOTS && renderer->atlas.pixels )
    {
        struct TRSPK_AtlasTile tile;
        /* A deferred widget-model draw samples the world atlas when it is
         * ISSUED, not when it was recorded; clearing this tile now would
         * reach the GPU on the next atlas upload, ahead of that draw. Issue
         * what is recorded first. */
        if( renderer->in2d )
            es3_ui_flush(renderer);
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
es3_map_atlas_uv(
    int slot,
    float local_u,
    float local_v,
    float* out_u,
    float* out_v)
{
    const float cell = (float)TRSPK_ATLAS_TILE / (float)ES3_ATLAS_DIM;
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
    *out_u = (float)(index & (ES3_ATLAS_COLS - 1u)) * cell + local_u * cell;
    *out_v = (float)(index / ES3_ATLAS_COLS) * cell + local_v * cell;
}

/* ---- per-frame stream sets --------------------------------------------------- */

/* Rotate every stream set onto this frame's buffer. */
void
es3_stream_sets_begin_frame(struct TRSPK_Renderer_ES3* renderer)
{
    struct ES3StreamSet* sets[4];
    uint32_t set_index;
    renderer->frame_slot = (renderer->frame_slot + 1u) % ES3_FRAMES_IN_FLIGHT;
    sets[0] = &renderer->index_stream;
    sets[1] = &renderer->dynamic_stream;
    sets[2] = &renderer->frame_stream;
    sets[3] = &renderer->ui_stream;
    for( set_index = 0u; set_index < 4u; set_index++ )
    {
        struct ES3StreamSet* set = sets[set_index];
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
 * buffer was last read ES3_FRAMES_IN_FLIGHT frames ago, so the write
 * never lands on an outstanding draw. Returns the byte offset the payload
 * landed at.
 *
 * Growth has two shapes, and picking the wrong one is invisible until a
 * model blinks out.
 *
 * glBufferData(NULL) -- orphaning -- replaces the store and the bytes already
 * in [0, offset) are GONE from the new one. What GL guarantees is only that
 * draws already ISSUED against the old store keep reading it. So orphaning is
 * right in exactly two cases:
 *
 *   offset == 0                 nothing appended this frame is lost;
 *   earlier_appends_drawn       every earlier append of this frame has had
 *                               its draw issued (the immediate 2D path draws
 *                               right after each append), so losing the
 *                               bytes loses nothing a draw still wants.
 *
 * The third case is real on this lane and used to be a contract violation:
 * es3_upload_group appends one retained group per dirty group and NOTHING is
 * drawn until the sequence is issued at end of frame, so the second or later
 * group to cross the capacity orphaned every group uploaded before it. Their
 * draws then read a prefix that had been discarded -- which is a model, or a
 * run of them, missing for exactly the frames on which the stream grew.
 * Intermittent by construction: growth doubles the capacity, so it does not
 * recur until the scene grows again.
 *
 * It survived because the guard was `assert(offset == 0 || drawn)` and the
 * shipping build is -DNDEBUG. It was WRITTEN because this reasoning came
 * across from the ES 2.0 core, whose comment says "ES 2.0 has no copy between
 * buffers and no read-back, so nothing here can carry them over" -- true
 * there, and false here. **ES 3.0 has glCopyBufferSubData.** So the third
 * case is now handled rather than asserted away: grow into a fresh store and
 * copy the frame's existing prefix across on the GPU, with no read-back and
 * no CPU copy.
 *
 * That replaces the buffer NAME, so a caller that caches it re-reads
 * set->buffers[slot] afterwards. The VAOs need no help: es3_bind_stream
 * re-specifies a binding whose buffer object changed.
 */
uint32_t
es3_stream_set_append(
    struct ES3StreamSet* set,
    uint32_t slot,
    GLenum target,
    uint32_t initial_bytes,
    const void* data,
    uint32_t bytes,
    bool earlier_appends_drawn)
{
    uint32_t offset = set->head;
    assert(set->buffers[slot]);
    if( offset + bytes > set->capacities[slot] )
    {
        uint32_t capacity = set->capacities[slot] ? set->capacities[slot] : initial_bytes;
        while( capacity < offset + bytes )
            capacity *= 2u;
        if( offset == 0u || earlier_appends_drawn )
        {
            /* Nothing this frame still needs the old store. */
            glBindBuffer(target, set->buffers[slot]);
            glBufferData(target, (GLsizeiptr)capacity, NULL, GL_DYNAMIC_DRAW);
        }
        else
        {
            /*
             * Carry this frame's prefix into the bigger store.
             *
             * COPY_READ/COPY_WRITE and not `target`: they are plain context
             * bindings with no other meaning, so nothing the caller set up is
             * disturbed -- and GL_ELEMENT_ARRAY_BUFFER, which one caller uses,
             * is VERTEX ARRAY OBJECT state that binding through `target` here
             * would write into whichever VAO happens to be bound.
             */
            GLuint grown = 0u;
            glGenBuffers(1, &grown);
            assert(grown);
            glBindBuffer(GL_COPY_WRITE_BUFFER, grown);
            glBufferData(GL_COPY_WRITE_BUFFER, (GLsizeiptr)capacity, NULL, GL_DYNAMIC_DRAW);
            glBindBuffer(GL_COPY_READ_BUFFER, set->buffers[slot]);
            glCopyBufferSubData(
                GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, (GLsizeiptr)offset);
            glBindBuffer(GL_COPY_READ_BUFFER, 0);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            glDeleteBuffers(1, &set->buffers[slot]);
            set->buffers[slot] = grown;
        }
        set->capacities[slot] = capacity;
    }
    glBindBuffer(target, set->buffers[slot]);
    glBufferSubData(target, (GLintptr)offset, (GLsizeiptr)bytes, data);
    set->head = offset + ((bytes + 3u) & ~3u);
    return offset;
}

static void
es3_stream_set_destroy(struct ES3StreamSet* set)
{
    uint32_t slot;
    for( slot = 0u; slot < ES3_FRAMES_IN_FLIGHT; slot++ )
        if( set->buffers[slot] )
            glDeleteBuffers(1, &set->buffers[slot]);
    memset(set, 0, sizeof(*set));
}

/* ---- retained groups --------------------------------------------------------- */

static bool
es3_upload_group(
    struct TRSPK_Renderer_ES3* renderer,
    struct ES3ModelGroup* group)
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
        es3_bind_array_buffer(renderer, group->vbo_gpu);
    }

    if( group->reset_each_frame )
    {
        /* Rebuilt wholesale every frame, so it goes into this frame's buffer
         * of the dynamic stream set (see ES3_FRAMES_IN_FLIGHT). */
        uint32_t offset;
        byte_count = (size_t)vertex_count * sizeof(struct TRSPK_VertexGLES2);
        offset = es3_stream_set_append(
            &renderer->dynamic_stream,
            renderer->frame_slot,
            GL_ARRAY_BUFFER,
            ES3_DYNAMIC_STREAM_INIT_BYTES,
            group->vbo_cpu->vertices.as_gles2,
            (uint32_t)byte_count,
            false);
        /* Growth may have replaced the store; the group's name is this
         * frame's slot, whatever it now is. @see es3_stream_set_append. */
        group->vbo_gpu = renderer->dynamic_stream.buffers[renderer->frame_slot];
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
        uint32_t capacity = group->gpu_capacity ? group->gpu_capacity : ES3_GPU_BUFFER_INIT;
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

void
es3_reset_group(struct ES3ModelGroup* group)
{
    assert(group);
    if( group->arena )
        trspk_modelarena_clear(group->arena);
}

/* ---- static batches (Batch16 pages) ------------------------------------------ */

static bool
es3_upload_dirty_static_batches(struct TRSPK_Renderer_ES3* renderer);

static void
es3_mark_active_static_batches_dirty(struct TRSPK_Renderer_ES3* renderer)
{
    uint32_t batch_slot;
    assert(renderer);
    for( batch_slot = 0u; batch_slot < renderer->static_batch_count; batch_slot++ )
    {
        struct ES3StaticBatch* batch = &renderer->static_batches[batch_slot];
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
es3_grow_static_batches(
    struct TRSPK_Renderer_ES3* renderer,
    uint32_t needed)
{
    struct ES3StaticBatch* grown;
    uint32_t capacity;
    if( needed <= renderer->static_batch_capacity )
        return;
    capacity = renderer->static_batch_capacity ? renderer->static_batch_capacity : 8u;
    while( capacity < needed )
        capacity *= 2u;
    grown = (struct ES3StaticBatch*)realloc(
        renderer->static_batches, (size_t)capacity * sizeof(*grown));
    assert(grown);
    memset(
        grown + renderer->static_batch_capacity,
        0,
        (size_t)(capacity - renderer->static_batch_capacity) * sizeof(*grown));
    renderer->static_batches = grown;
    renderer->static_batch_capacity = capacity;
}

int
es3_static_batch_slot(
    struct TRSPK_Renderer_ES3* renderer,
    int batch_id,
    bool create)
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
    es3_grow_static_batches(renderer, renderer->static_batch_count + 1u);
    slot = renderer->static_batch_count++;
    renderer->static_batches[slot].batch_id = batch_id;
    renderer->static_batches[slot].cpu = trspk_batch16_create(TRSPK_VERTEX_FORMAT_GLES2);
    assert(renderer->static_batches[slot].cpu);
    return (int)slot;
}

void
es3_rebuild_batch_pose_table(struct TRSPK_Renderer_ES3* renderer)
{
    uint32_t batch_slot;
    assert(renderer);
    trspk_pose_table_clear(&renderer->batch_poses);
    for( batch_slot = 0u; batch_slot < renderer->static_batch_count; batch_slot++ )
    {
        const struct ES3StaticBatch* batch = &renderer->static_batches[batch_slot];
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
                entry_index > ES3_BATCH_POSE_ENTRY_MASK ||
                batch_slot > ES3_BATCH_POSE_SLOT_MASK )
                continue;
            page_id = batch->page_ids[entry->chunk_index];
            if( page_id >= renderer->static_page_count || !renderer->static_pages[page_id].valid )
                continue;
            trspk_pose_table_set(
                &renderer->batch_poses,
                entry->element_id,
                entry->anim_index,
                entry->pose_id,
                ES3_BATCH_POSE_FLAG | (batch_slot << ES3_BATCH_POSE_SLOT_SHIFT) |
                    entry_index);
        }
    }
    es3_static_primary_rebuild(renderer);
}

static bool
es3_grow_static_pages(
    struct TRSPK_Renderer_ES3* renderer,
    uint32_t needed)
{
    struct ES3StaticPageRef* grown;
    uint32_t capacity;
    if( needed <= renderer->static_page_capacity )
        return true;
    if( needed > ES3_BATCH_PAGE_LIMIT )
        return false;
    capacity = renderer->static_page_capacity ? renderer->static_page_capacity : 32u;
    while( capacity < needed )
    {
        if( capacity >= ES3_BATCH_PAGE_LIMIT / 2u )
        {
            capacity = ES3_BATCH_PAGE_LIMIT;
            break;
        }
        capacity *= 2u;
    }
    grown = (struct ES3StaticPageRef*)realloc(
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
es3_static_batch_ensure_chunk_storage(
    struct ES3StaticBatch* batch,
    uint32_t chunk_count)
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
es3_static_batch_assign_page(
    struct TRSPK_Renderer_ES3* renderer,
    uint32_t batch_slot,
    uint32_t chunk_index)
{
    struct ES3StaticBatch* batch = &renderer->static_batches[batch_slot];
    const struct TRSPK_Batch16Chunk* chunk;
    struct ES3StaticPageRef* page;
    uint32_t page_id;
    uint32_t needed;
    assert(chunk_index < batch->page_id_capacity);
    chunk = trspk_batch16_get_chunk(batch->cpu, chunk_index);
    needed = chunk ? chunk->vertex_count : 0u;
    page_id = batch->page_ids[chunk_index];
    if( page_id == UINT32_MAX )
    {
        if( renderer->static_page_count >= ES3_BATCH_PAGE_LIMIT ||
            !es3_grow_static_pages(renderer, renderer->static_page_count + 1u) )
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
     * out (es3_compact_static_pages). */
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
es3_compact_static_pages(struct TRSPK_Renderer_ES3* renderer)
{
    uint32_t page_id;
    uint32_t used = 0u;
    assert(renderer);
    for( page_id = 0u; page_id < renderer->static_page_count; page_id++ )
    {
        struct ES3StaticPageRef* page = &renderer->static_pages[page_id];
        const struct ES3StaticBatch* batch;
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
    es3_mark_active_static_batches_dirty(renderer);
    return used;
}

void
es3_invalidate_batch_pages(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ES3StaticBatch* batch)
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
es3_ensure_static_batch_vbo(
    struct TRSPK_Renderer_ES3* renderer,
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
                   : ES3_STATIC_BATCH_VBO_INIT_VERTICES;
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
    es3_bind_array_buffer(renderer, renderer->static_batch_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)byte_capacity, NULL, GL_STATIC_DRAW);
    if( !es3_check_error("static batch page buffer") )
        return false;
    renderer->static_batch_gpu_vertex_capacity = capacity;
    *out_recreated = true;
    return true;
}

static bool
es3_upload_static_batch_chunk(
    struct TRSPK_Renderer_ES3* renderer,
    struct ES3StaticBatch* batch,
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
        es3_bind_array_buffer(renderer, renderer->static_batch_vbo);
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
es3_upload_dirty_static_batches(struct TRSPK_Renderer_ES3* renderer)
{
    uint32_t batch_slot;
    bool recreated = false;
    assert(renderer);
    if( !renderer->static_batch_upload_pending )
        return true;
    if( !es3_ensure_static_batch_vbo(
            renderer, renderer->static_batch_gpu_vertex_used, &recreated) )
        return false;
    if( recreated )
        es3_mark_active_static_batches_dirty(renderer);
    for( batch_slot = 0u; batch_slot < renderer->static_batch_count; batch_slot++ )
    {
        struct ES3StaticBatch* batch = &renderer->static_batches[batch_slot];
        uint32_t chunk_count;
        uint32_t chunk;
        if( !batch->active || !batch->cpu )
            continue;
        chunk_count = trspk_batch16_chunk_count(batch->cpu);
        for( chunk = 0u; chunk < chunk_count; chunk++ )
            if( !es3_upload_static_batch_chunk(renderer, batch, chunk) )
                return false;
    }
    renderer->static_batch_upload_pending = false;
    return true;
}

bool
es3_static_batch_commit(
    struct TRSPK_Renderer_ES3* renderer,
    uint32_t batch_slot)
{
    struct ES3StaticBatch* batch;
    uint32_t chunk_count;
    uint32_t chunk;
    bool recreated = false;

    assert(renderer);
    assert(batch_slot < renderer->static_batch_count);
    batch = &renderer->static_batches[batch_slot];
    assert(batch->cpu);
    batch->active = false;
    chunk_count = trspk_batch16_chunk_count(batch->cpu);
    es3_static_batch_ensure_chunk_storage(batch, chunk_count);
    es3_invalidate_batch_pages(renderer, batch);
    for( chunk = 0u; chunk < chunk_count; chunk++ )
        if( !es3_static_batch_assign_page(renderer, batch_slot, chunk) )
            goto fail;
    /* The tail ran past the buffer: pack the holes the rebuilt chunks left
     * before buying a bigger buffer, since either way everything re-sends. */
    if( renderer->static_batch_vbo &&
        renderer->static_batch_gpu_vertex_used > renderer->static_batch_gpu_vertex_capacity )
        (void)es3_compact_static_pages(renderer);
    if( !es3_ensure_static_batch_vbo(
            renderer, renderer->static_batch_gpu_vertex_used, &recreated) )
        goto fail;
    if( recreated || renderer->static_batch_upload_pending )
    {
        es3_mark_active_static_batches_dirty(renderer);
        if( !es3_upload_dirty_static_batches(renderer) )
            goto fail;
    }
    for( chunk = 0u; chunk < chunk_count; chunk++ )
        if( !es3_upload_static_batch_chunk(renderer, batch, chunk) )
            goto fail;
    batch->active = true;
    es3_rebuild_batch_pose_table(renderer);
    return true;

fail:
    es3_invalidate_batch_pages(renderer, batch);
    es3_rebuild_batch_pose_table(renderer);
    return false;
}

static bool
es3_resolve_static_page(
    struct TRSPK_Renderer_ES3* renderer,
    uint32_t page_id,
    struct TRSPK_Batch16Chunk** out_chunk)
{
    const struct ES3StaticPageRef* ref;
    struct ES3StaticBatch* batch;
    struct TRSPK_Batch16Chunk* chunk;
    assert(renderer);
    assert(out_chunk);
    if( page_id >= renderer->static_page_count || !renderer->static_pages[page_id].valid )
        return false;
    ref = &renderer->static_pages[page_id];
    if( ref->batch_slot >= renderer->static_batch_count )
        return false;
    batch = &renderer->static_batches[ref->batch_slot];
    if( !batch->active || !batch->cpu || ref->chunk_index >= trspk_batch16_chunk_count(batch->cpu) )
        return false;
    chunk = trspk_batch16_get_chunk(batch->cpu, ref->chunk_index);
    if( !chunk )
        return false;
    *out_chunk = chunk;
    return true;
}

bool
es3_binding_cpu_source(
    struct TRSPK_Renderer_ES3* renderer,
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
    if( binding == ES3_STATIC_PAGE_BINDING )
    {
        struct TRSPK_Batch16Chunk* chunk = NULL;
        if( !es3_resolve_static_page(renderer, page_id, &chunk) )
            return false;
        *out_vbo = chunk->vbo;
        *out_triangles = &chunk->triangles;
        return *out_vbo != NULL;
    }
    if( binding == ES3_FRAME_STREAM_BINDING )
    {
        *out_vbo = renderer->frame_stream_cpu;
        *out_triangles = &renderer->frame_stream_triangles;
        return *out_vbo != NULL;
    }
    return false;
}

/* ---- pose tables and the static arena ------------------------------------------ */

static void
es3_rebuild_static_pose_table(struct TRSPK_Renderer_ES3* renderer)
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
void
es3_compact_static_group(struct TRSPK_Renderer_ES3* renderer)
{
    struct TRSPK_ModelArena* arena;
    struct TRSPK_ModelArenaGCResult result;
    assert(renderer);
    arena = renderer->groups[TRSPK_VBO_GROUP_STATIC].arena;
    if( !arena )
        return;
    result = trspk_modelarena_gc(arena);
    if( result.did_compact )
        es3_rebuild_static_pose_table(renderer);
}

static bool
es3_pose_element_is_retained(
    const struct TRSPK_Renderer_ES3* renderer,
    int element_id)
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
es3_pose_track_is_retained(
    const struct TRSPK_Renderer_ES3* renderer,
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
bool
es3_bake_pose_vertices(
    struct TRSPK_Renderer_ES3* renderer,
    struct TRSPK_VBO* vbo,
    struct TRSPK_Triangles* triangles,
    uint32_t vertex_base,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position,
    const int* face_order,
    int order_count,
    bool ordered_painter)
{
    struct TRSPK_WorldPlacement placement;
    int face_count;
    uint32_t order_index;
    uint32_t written_count;

    assert(renderer);
    /* ordered_painter is the CALLER's knowledge: only the painter renderer
     * bakes an actor into the frame stream in sorted face order, and only it
     * knows that it is doing so. @see es3p_draw_model. */
    assert(!ordered_painter || face_order);
    assert(renderer->scene);
    assert(vbo);
    assert(triangles);
    face_count = trspk_toridraw_face_count(model_handle);
    if( face_count <= 0 )
        return false;
    /* With a face order the pose is written in THAT order -- the painter
     * path's sorted actors -- and only the faces the order names. */
    written_count =
        face_order ? (uint32_t)(order_count > 0 ? order_count : 0) : (uint32_t)face_count;
    trspk_toridraw_placement_init(&placement, world_position);
    float* world_xyz = NULL;
    struct ToriDraw_Model* full_model = NULL;
    if( renderer->actor_world_cache_enabled && ordered_painter &&
        ToriDraw_ModelKindIsFull(model_handle.kind) )
    {
        full_model = (struct ToriDraw_Model*)ToriDraw_ModelRead(model_handle);
        if( written_count * 3u > (uint32_t)full_model->vertex_count )
        {
            if( (uint32_t)full_model->vertex_count > renderer->actor_world_capacity )
            {
                float* grown = realloc(
                    renderer->actor_world_xyz,
                    (size_t)full_model->vertex_count * 3 * sizeof(float));
                if( grown )
                {
                    renderer->actor_world_xyz = grown;
                    renderer->actor_world_capacity = (uint32_t)full_model->vertex_count;
                }
            }
            if( (uint32_t)full_model->vertex_count <= renderer->actor_world_capacity )
            {
                world_xyz = renderer->actor_world_xyz;
                trspk_toridraw_world_vertices(full_model, &placement, world_xyz);
            }
        }
    }
#if defined(TORIRS_BAKE_CHAIN_CAPTURE)
    es3_bake_capture_begin(renderer, model_handle, world_position, face_order, order_count);
#endif

#if !defined(TORIRS_BAKE_CHAIN_CAPTURE) && !defined(TORIRS_BAKE_VERIFY)
    /* This lane's fast encoder, or a decline. @see es3_bake_ordered_fast at
     * the top of this file: it is written out per lane, not selected. */
    if( es3_bake_ordered_fast(
            vbo, vertex_base, full_model, face_order, written_count, world_xyz, ordered_painter) )
        return true;
#endif

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
        int config = ES3_TRIANGLE_UNTEXTURED;

        bool baked = false;
        if( face_index < (uint32_t)face_count )
        {
            if( world_xyz )
            {
                trspk_toridraw_bake_face_cached(
                    full_model,
                    face_index,
                    &placement,
                    NULL,
                    true,
                    TRSPK_BAKE_COLOR_ARGB,
                    world_xyz,
                    &face);
                baked = true;
            }
            else
                baked = trspk_toridraw_bake_face_handle(
                    model_handle, face_index, &placement, NULL, true, TRSPK_BAKE_COLOR_ARGB, &face);
        }
        if( !baked )
        {
            /* A skipped face still owns its triplet in an ordered bake: leave
             * it fully transparent so the alpha test drops it. */
            if( face_order )
            {
                if( !ordered_painter )
                    trspk_triangles_set(
                        triangles,
                        trspk_triangles_index_from_vertex(vertex),
                        ES3_TRIANGLE_UNTEXTURED);
                trspk_vbo_write_vertex_gles2(
                    vbo,
                    vertex,
                    0.0f,
                    0.0f,
                    0.0f,
                    0u,
                    0.5f,
                    0.5f,
                    0u,
                    0u,
                    TRSPK_VERTEX_GLES2_ANIM_STILL,
                    TRSPK_VERTEX_GLES2_ANIM_STILL);
                trspk_vbo_write_vertex_gles2(
                    vbo,
                    vertex + 1u,
                    0.0f,
                    0.0f,
                    0.0f,
                    0u,
                    0.5f,
                    0.5f,
                    0u,
                    0u,
                    TRSPK_VERTEX_GLES2_ANIM_STILL,
                    TRSPK_VERTEX_GLES2_ANIM_STILL);
                trspk_vbo_write_vertex_gles2(
                    vbo,
                    vertex + 2u,
                    0.0f,
                    0.0f,
                    0.0f,
                    0u,
                    0.5f,
                    0.5f,
                    0u,
                    0u,
                    TRSPK_VERTEX_GLES2_ANIM_STILL,
                    TRSPK_VERTEX_GLES2_ANIM_STILL);
            }
            continue;
        }

#if defined(TORIRS_BAKE_CHAIN_CAPTURE)
        es3_bake_capture_face(&face);
#endif
#if defined(TORIRS_BAKE_VERIFY)
        if( world_xyz )
        {
            struct TRSPK_ToriDrawBakeFaceVerts reference;
            trspk_toridraw_bake_face_handle(
                model_handle,
                face_index,
                &placement,
                renderer->scene,
                true,
                TRSPK_BAKE_COLOR_ARGB,
                &reference);
            struct BakeChainFace a = bake_chain_face(&face), b = bake_chain_face(&reference);
            if( memcmp(&a, &b, sizeof(a)) )
            {
                fprintf(stderr, "bake verification FAILED\n");
                abort();
            }
            static unsigned matched = 0;
            if( (++matched % 10000) == 0 )
                fprintf(stderr, "bake verification: %u real faces matched\n", matched);
        }
#endif
        if( face.tex_id >= 0 )
        {
            int slot = es3_ensure_texture(renderer, face.tex_id);
            config = face.tex_id;
            if( slot >= 0 )
            {
                tile_col = (uint8_t)((uint32_t)slot & (ES3_ATLAS_COLS - 1u));
                tile_row = (uint8_t)((uint32_t)slot / ES3_ATLAS_COLS);
                es3_texture_anim_bytes(
                    es3_scene_texture(renderer, face.tex_id), &anim_u, &anim_v);
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

        if( !ordered_painter )
            trspk_triangles_set(triangles, trspk_triangles_index_from_vertex(vertex), config);
        trspk_vbo_write_vertex_gles2(
            vbo,
            vertex,
            face.wx_a,
            face.wy_a,
            face.wz_a,
            es3_argb_to_rgba_bytes(face.argb_a),
            ua,
            va,
            tile_col,
            tile_row,
            anim_u,
            anim_v);
        trspk_vbo_write_vertex_gles2(
            vbo,
            vertex + 1u,
            face.wx_b,
            face.wy_b,
            face.wz_b,
            es3_argb_to_rgba_bytes(face.argb_b),
            ub,
            vb,
            tile_col,
            tile_row,
            anim_u,
            anim_v);
        trspk_vbo_write_vertex_gles2(
            vbo,
            vertex + 2u,
            face.wx_c,
            face.wy_c,
            face.wz_c,
            es3_argb_to_rgba_bytes(face.argb_c),
            uc,
            vc,
            tile_col,
            tile_row,
            anim_u,
            anim_v);
    }
    /* Once for the model rather than three times per face -- and as a RANGE,
     * because this model is the only part of a shared retained buffer that
     * changed. */
#if defined(TORIRS_BAKE_CHAIN_CAPTURE)
    es3_bake_capture_end();
#endif
#if defined(TORIRS_BAKE_VERIFY)
    if( ordered_painter && world_xyz && !full_model->face_textures )
    {
        /* Verify the entire ordered direct stream, including placeholders,
         * against the generic final vertex writer above. The normal fast
         * return is suppressed in this diagnostic so both paths execute. */
        size_t bytes = (size_t)written_count * 3u * sizeof(struct TRSPK_VertexGLES2);
        struct TRSPK_VertexGLES2* direct = malloc(bytes ? bytes : 1u);
        if( !direct )
        {
            fprintf(stderr, "direct bake verification allocation failed\n");
            abort();
        }
        /* This renderer's encoder; @see es3_bake_ordered_fast. */
        trspk_toridraw_gles2_untextured_words(
            full_model, face_order, written_count, world_xyz, direct);
        if( memcmp(direct, &vbo->vertices.as_gles2[vertex_base], bytes) )
        {
            fprintf(stderr, "direct bake verification FAILED: %u ordered faces\n", written_count);
            abort();
        }
        memcpy(&vbo->vertices.as_gles2[vertex_base], direct, bytes);
        free(direct);
        static unsigned matched_models, matched_faces;
        matched_faces += written_count;
        if( (++matched_models % 100u) == 0u )
            fprintf(
                stderr,
                "direct bake verification: %u models, %u packed faces matched\n",
                matched_models,
                matched_faces);
    }
#endif
    trspk_vbo_mark_dirty_range(vbo, vertex_base, written_count * 3u);
    return true;
}

uint32_t
es3_bake_into_arena(
    struct TRSPK_Renderer_ES3* renderer,
    struct ES3ModelGroup* group,
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
        TORIRS_ERR(
            "%s: model has %lu vertices and cannot fit a 16-bit page\n",
            g_es3_name,
            (unsigned long)vertex_count);
        return UINT32_MAX;
    }
    anim_index = es3_clampi(anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1);
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
                es3_compact_static_group(renderer);
        }
    }
    slot_index = trspk_modelarena_load(group->arena, arena_element_id, arena_pose_id, vertex_count);
    model_slot = trspk_modelarena_get(group->arena, slot_index);
    if( !model_slot || !es3_bake_pose_vertices(
                           renderer,
                           group->vbo_cpu,
                           &group->triangles,
                           model_slot->vertex_base,
                           model_handle,
                           world_position,
                           NULL,
                           0,
                           false) )
        return UINT32_MAX;
    if( update_pose_table )
    {
        trspk_pose_table_set(
            &renderer->poses, element_id, anim_index, pose_id, model_slot->vertex_base);
    }
    return model_slot->vertex_base;
}

bool
es3_model_unload(
    struct TRSPK_Renderer_ES3* renderer,
    int element_id)
{
    assert(renderer);
    /* Individual unloads own only the arena. Batch geometry and its pose map
     * remain immutable until the matching batch rebuild/clear. */
    if( element_id < 0 || !renderer->groups[TRSPK_VBO_GROUP_STATIC].arena ||
        !es3_pose_element_is_retained(renderer, element_id) )
        return false;
    trspk_modelarena_unload_element(renderer->groups[TRSPK_VBO_GROUP_STATIC].arena, element_id);
    trspk_pose_table_remove_element(&renderer->poses, element_id);
    es3_compact_static_group(renderer);
    return true;
}

bool
es3_animation_track_unload(
    struct TRSPK_Renderer_ES3* renderer,
    int element_id,
    int anim_index)
{
    struct TRSPK_ModelArena* arena;
    uint32_t slot_index;
    assert(renderer);
    if( element_id < 0 || anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT )
        return false;
    arena = renderer->groups[TRSPK_VBO_GROUP_STATIC].arena;
    if( !arena || !es3_pose_track_is_retained(renderer, element_id, anim_index) )
        return false;
    for( slot_index = 0u; slot_index < arena->slot_count; slot_index++ )
    {
        const struct TRSPK_ModelSlot* slot = &arena->slots[slot_index];
        if( trspk_modelslot_is_alive(slot) && slot->element_id == element_id &&
            slot->pose_id % TRSPK_POSE_TRACK_COUNT == anim_index )
            trspk_modelarena_unload(arena, slot_index);
    }
    trspk_pose_table_remove_track(&renderer->poses, element_id, anim_index);
    es3_compact_static_group(renderer);
    return true;
}


/*
 * Whether ANIM_LOAD's command can be baked at all, and which pose track it
 * names. The track index is clamped here so every caller keys the arena,
 * the pose table and its own bookkeeping with the same number.
 */
bool
es3_animation_load_check(
    const struct ToriRS_RenderCommand_AnimLoad* command,
    int* out_anim_index)
{
    assert(command);
    assert(out_anim_index);
    if( command->element_id < 0 || !command->animation || command->animation->frame_count <= 0 ||
        !ToriDraw_ModelKindIsFull(command->model.kind) || !command->model.u.model.model )
        return false;
    if( !command->animation->skeletal &&
        (!command->animation->base || !command->animation->frames) )
        return false;
    *out_anim_index = es3_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1);
    return true;
}

/*
 * One frame of the command's animation, posed into a throwaway model at
 * render scale. Never NULL; the caller bakes it and then ToriDraw_ModelFree's
 * it. Valid only for a command es3_animation_load_check accepted.
 */
struct ToriDraw_Model*
es3_animation_pose_frame(
    const struct ToriRS_RenderCommand_AnimLoad* command,
    int frame)
{
    struct ToriDraw_Animation* animation;
    struct ToriDraw_SkeletalAnim* skeletal;
    struct ToriDraw_Model* source;
    struct ToriDraw_Model* baked;
    bool posed = false;

    assert(command);
    assert(command->animation);
    assert(frame >= 0);
    assert(frame < command->animation->frame_count);
    animation = command->animation;
    skeletal = animation->skeletal;
    source = command->model.u.model.model;
    baked = ToriDraw_ModelCopy(source);
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
        if( skeletal->frame_count > 0 && skeletal->matrices &&
            baked->animaya_vertex_count > 0 && baked->animaya_group_counts &&
            baked->animaya_groups && baked->animaya_scales )
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
    return baked;
}

bool
es3_reserve_model_indices(struct TRSPK_Renderer_ES3* renderer, uint32_t needed)
{
    uint32_t* grown;
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
    grown = (uint32_t*)realloc(renderer->model_indices, (size_t)capacity * sizeof(*grown));
    assert(grown);
    renderer->model_indices = grown;
    renderer->model_index_capacity = capacity;
    return true;
}

/* ---- vertex streams ------------------------------------------------------------- */

/*
 * The world vertex layout, specified into whatever VAO is bound.
 *
 * Offsets are from the start of the buffer and never move: an index is
 * 32-bit and reaches the whole buffer, so nothing here is re-pointed to
 * select a page the way the ES2 renderer's binder is. The tile/scroll word
 * is read with glVertexAttribIPointer -- the shader receives a uvec4 and
 * does no de-normalising multiply -- which is a GLES3 entry point with no
 * ES2 equivalent.
 */
static void
es3_specify_world_layout(uint32_t byte_offset)
{
    glEnableVertexAttribArray(ES3_ATTRIB_POSITION);
    glVertexAttribPointer(
        ES3_ATTRIB_POSITION,
        3,
        GL_FLOAT,
        GL_FALSE,
        ES3_VERTEX_STRIDE,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct TRSPK_VertexGLES2, position)));
    glEnableVertexAttribArray(ES3_ATTRIB_COLOR);
    glVertexAttribPointer(
        ES3_ATTRIB_COLOR,
        4,
        GL_UNSIGNED_BYTE,
        GL_TRUE,
        ES3_VERTEX_STRIDE,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct TRSPK_VertexGLES2, rgba)));
    glEnableVertexAttribArray(ES3_ATTRIB_TEXCOORD);
    glVertexAttribPointer(
        ES3_ATTRIB_TEXCOORD,
        2,
        GL_FLOAT,
        GL_FALSE,
        ES3_VERTEX_STRIDE,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct TRSPK_VertexGLES2, texcoord)));
    glEnableVertexAttribArray(ES3_ATTRIB_TEXINFO);
    glVertexAttribIPointer(
        ES3_ATTRIB_TEXINFO,
        4,
        GL_UNSIGNED_BYTE,
        ES3_VERTEX_STRIDE,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct TRSPK_VertexGLES2, tile_col)));
}

/*
 * Bind a vertex array object through the state cache.
 *
 * GLES3 keeps the ELEMENT array binding inside the VAO, so changing VAO
 * also changes which index buffer is current. Both caches are invalidated
 * here and the callers that need either re-establish it.
 */
void
es3_bind_vao(struct TRSPK_Renderer_ES3* renderer, GLuint vao)
{
    assert(renderer);
    if( renderer->vao_bound == vao )
        return;
    glBindVertexArray(vao);
    renderer->vao_bound = vao;
    /* The array binding is context state that the VAO's attribute setup
     * overwrites, so it is genuinely unknown after a switch. The ELEMENT
     * binding is not: it belongs to the VAO now bound, and is tracked per VAO
     * (vao_element_buffer) precisely so a switch costs nothing. */
    renderer->bound_array_buffer = 0u;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATTRIB_REBINDS, 1);
}

/*
 * The vertex array object for one world binding.
 *
 * Built the first time the binding has a GPU buffer and re-specified only
 * when that buffer OBJECT changes -- a stream set rotating to the next
 * frame's buffer, or a retained buffer recreated by a grow. In the steady
 * state a stream change is one glBindVertexArray instead of the four
 * glVertexAttribPointer calls the ES2 renderer issues, each of which is a
 * call across the JavaScript boundary in a browser.
 */
/*
 * The vertex array object for one world binding.
 *
 * Built the first time the binding has a GPU buffer and re-specified only
 * when that buffer object or the binding's per-frame base vertex changes: a
 * stream set rotating to the next frame's buffer, a retained buffer
 * recreated by a grow, or a per-frame stream landing at a new offset. Both
 * are once-a-frame events, so in the steady state a stream change is one
 * glBindVertexArray -- where the ES2 renderer re-issues four
 * glVertexAttribPointer calls, each of them a call across the JavaScript
 * boundary in a browser, hundreds of times a frame.
 *
 * Indices are relative to the base this binds at, which for the two
 * retained bindings is zero: a 32-bit index addresses the whole buffer, so
 * unlike the ES2 renderer nothing here re-points to reach a page.
 */
bool
es3_bind_stream(struct TRSPK_Renderer_ES3* renderer, uint32_t binding)
{
    GLuint buffer;
    uint32_t base_vertex;
    uint64_t byte_offset;
    assert(renderer);
    assert(binding < ES3_BINDING_COUNT);
    if( binding < TRSPK_VBO_GROUP_COUNT )
    {
        buffer = renderer->groups[binding].vbo_gpu;
        base_vertex = renderer->groups[binding].gpu_base_vertex;
    }
    else if( binding == ES3_STATIC_PAGE_BINDING )
    {
        buffer = renderer->static_batch_vbo;
        base_vertex = 0u;
    }
    else if( binding == ES3_FRAME_STREAM_BINDING )
    {
        buffer = renderer->frame_stream_vbo;
        base_vertex = renderer->frame_stream_gpu_base;
    }
    else
        return false;
    if( !buffer )
        return false;
    byte_offset = (uint64_t)base_vertex * sizeof(struct TRSPK_VertexGLES2);
    assert(byte_offset <= (uint64_t)INT32_MAX);
    if( !renderer->vao_world[binding] )
    {
        glGenVertexArrays(1, &renderer->vao_world[binding]);
        if( !renderer->vao_world[binding] )
            return false;
        renderer->vao_world_buffer[binding] = 0u;
        renderer->vao_element_buffer[binding] = 0u;
    }
    es3_bind_vao(renderer, renderer->vao_world[binding]);
    if( renderer->vao_world_buffer[binding] != buffer ||
        renderer->vao_world_base[binding] != (uint32_t)byte_offset )
    {
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        renderer->bound_array_buffer = buffer;
        es3_specify_world_layout((uint32_t)byte_offset);
        renderer->vao_world_buffer[binding] = buffer;
        renderer->vao_world_base[binding] = (uint32_t)byte_offset;
    }
    renderer->stream_buffer = buffer;
    renderer->stream_byte_offset = (uint32_t)byte_offset;
    renderer->stream_layout = ES3_STREAM_WORLD;
    return true;
}

/*
 * The 2D stream's two layouts, each in its own vertex array object over this
 * frame's UI buffer.
 *
 * The 2D stream is a RING: a flush appends at a byte offset that moves
 * through the frame, so unlike the world layouts these attribute pointers
 * really do have to follow an offset. GLES3 keeps that offset inside the
 * VAO, so the re-specification happens once per (layout, offset) change into
 * a VAO that is then bound by name -- and, crucially, switching between the
 * UI layout and the rotmask layout costs one bind rather than four pointer
 * calls plus the enable/disable dance the shared fourth attribute slot needs
 * on ES2.
 */
static GLuint
es3_ui_vao_ensure(GLuint* vao)
{
    if( !*vao )
        glGenVertexArrays(1, vao);
    return *vao;
}

void
es3_bind_ui_stream(struct TRSPK_Renderer_ES3* renderer, uint32_t byte_offset)
{
    const GLsizei stride = (GLsizei)sizeof(struct ES3VertexUI);
    assert(renderer);
    if( !es3_ui_vao_ensure(&renderer->vao_ui) )
        return;
    es3_bind_vao(renderer, renderer->vao_ui);
    if( renderer->vao_ui_buffer == renderer->ui_vbo && renderer->vao_ui_offset == byte_offset )
    {
        renderer->stream_layout = ES3_STREAM_UI;
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, renderer->ui_vbo);
    renderer->bound_array_buffer = renderer->ui_vbo;
    glEnableVertexAttribArray(ES3_ATTRIB_POSITION);
    glVertexAttribPointer(
        ES3_ATTRIB_POSITION,
        3,
        GL_FLOAT,
        GL_FALSE,
        stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES3VertexUI, x)));
    glEnableVertexAttribArray(ES3_ATTRIB_TEXCOORD);
    glVertexAttribPointer(
        ES3_ATTRIB_TEXCOORD,
        2,
        GL_FLOAT,
        GL_FALSE,
        stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES3VertexUI, u)));
    glEnableVertexAttribArray(ES3_ATTRIB_COLOR);
    glVertexAttribPointer(
        ES3_ATTRIB_COLOR,
        4,
        GL_UNSIGNED_BYTE,
        GL_TRUE,
        stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES3VertexUI, rgba)));
    glEnableVertexAttribArray(ES3_ATTRIB_TEXINFO);
    glVertexAttribPointer(
        ES3_ATTRIB_TEXINFO,
        1,
        GL_FLOAT,
        GL_FALSE,
        stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES3VertexUI, sel)));
    renderer->vao_ui_buffer = renderer->ui_vbo;
    renderer->vao_ui_offset = byte_offset;
    renderer->stream_buffer = renderer->ui_vbo;
    renderer->stream_byte_offset = byte_offset;
    renderer->stream_layout = ES3_STREAM_UI;
}

void
es3_bind_rotmask_stream(struct TRSPK_Renderer_ES3* renderer, uint32_t byte_offset)
{
    const GLsizei stride = (GLsizei)sizeof(struct ES3VertexRotmask);
    assert(renderer);
    if( !es3_ui_vao_ensure(&renderer->vao_rotmask) )
        return;
    es3_bind_vao(renderer, renderer->vao_rotmask);
    if( renderer->vao_rotmask_buffer == renderer->ui_vbo &&
        renderer->vao_rotmask_offset == byte_offset )
    {
        renderer->stream_layout = ES3_STREAM_ROTMASK;
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, renderer->ui_vbo);
    renderer->bound_array_buffer = renderer->ui_vbo;
    glEnableVertexAttribArray(ES3_ATTRIB_POSITION);
    glVertexAttribPointer(
        ES3_ATTRIB_POSITION,
        3,
        GL_FLOAT,
        GL_FALSE,
        stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES3VertexRotmask, x)));
    glEnableVertexAttribArray(ES3_ATTRIB_TEXCOORD);
    glVertexAttribPointer(
        ES3_ATTRIB_TEXCOORD,
        2,
        GL_FLOAT,
        GL_FALSE,
        stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES3VertexRotmask, u)));
    glEnableVertexAttribArray(ES3_ATTRIB_COLOR);
    glVertexAttribPointer(
        ES3_ATTRIB_COLOR,
        4,
        GL_UNSIGNED_BYTE,
        GL_TRUE,
        stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES3VertexRotmask, rgba)));
    /* The mask uv shares the fourth attribute slot with the world's texinfo
     * and the UI's sampler select. On ES2 that sharing is a hazard -- the
     * slot is context state, so a layout change has to re-enable it by hand
     * and a mistake there silently feeds the shader a constant. Here each
     * layout owns a VAO and the slot is described once per layout. */
    glEnableVertexAttribArray(ES3_ATTRIB_MASK_TEXCOORD);
    glVertexAttribPointer(
        ES3_ATTRIB_MASK_TEXCOORD,
        2,
        GL_FLOAT,
        GL_FALSE,
        stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES3VertexRotmask, mask_u)));
    renderer->vao_rotmask_buffer = renderer->ui_vbo;
    renderer->vao_rotmask_offset = byte_offset;
    renderer->stream_buffer = renderer->ui_vbo;
    renderer->stream_byte_offset = byte_offset;
    renderer->stream_layout = ES3_STREAM_ROTMASK;
}

/*
 * The 2D ring. Every flush appends at the head instead of rewriting offset
 * 0, so the driver never has to synchronise a write against the draw that is
 * still reading. Wrapping orphans the whole buffer with glBufferData(NULL),
 * which is the ES2 idiom for "give me fresh storage, keep the old for the GPU".
 * `earlier_appends_drawn`: see es3_stream_set_append.
 */
uint32_t
es3_ring_upload(
    struct TRSPK_Renderer_ES3* renderer,
    const void* data,
    uint32_t bytes,
    bool earlier_appends_drawn)
{
    uint32_t offset;
    assert(renderer);
    assert(data);
    offset = es3_stream_set_append(
        &renderer->ui_stream,
        renderer->frame_slot,
        GL_ARRAY_BUFFER,
        ES3_UI_STREAM_INIT_BYTES,
        data,
        bytes,
        earlier_appends_drawn);
    renderer->ui_vbo = renderer->ui_stream.buffers[renderer->frame_slot];
    renderer->bound_array_buffer = renderer->ui_vbo;
    return offset;
}

/* ---- the world draw ------------------------------------------------------------- */

/*
 * The world pass's shared uniform block: the matrix and the texture clock.
 *
 * Uploaded ONCE per pass, before any world draw. The ES2 renderer cannot do
 * this -- a uniform there is program state, so it re-sends the matrix every
 * time the draw loop switches between the plain and the cutout program --
 * and the block is also what makes those switches cheap enough to stop
 * thinking about.
 *
 * The layout is std140: a mat4 followed by a vec4 whose x is the clock. The
 * clock is reduced modulo 128 on the CPU because speed / 128 texels per tick
 * means the scroll repeats every 128 ticks, and a float clock that never
 * grows past 128 keeps the fract() in the shader exact.
 */
void
es3_world_block_upload(struct TRSPK_Renderer_ES3* renderer)
{
    float block[20];
    assert(renderer);
    if( !renderer->world_ubo )
    {
        glGenBuffers(1, &renderer->world_ubo);
        assert(renderer->world_ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, renderer->world_ubo);
        glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)sizeof(block), NULL, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, ES3_WORLD_BLOCK_BINDING, renderer->world_ubo);
    }
    memcpy(block, renderer->model_view_projection, 16u * sizeof(float));
    block[16] = (float)fmod(renderer->frame_clock, 128.0);
    block[17] = 0.0f;
    block[18] = 0.0f;
    block[19] = 0.0f;
    glBindBuffer(GL_UNIFORM_BUFFER, renderer->world_ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)sizeof(block), block);
}

void
es3_use_world_program(
    struct TRSPK_Renderer_ES3* renderer,
    bool cutout)
{
    const struct ES3Program* program;
    assert(renderer);
    /* Always the fast pair on this renderer: the slow programs were the
     * control arm of TORIRS_ES3_FAST_SHADER, which defaulted on everywhere.
     * @see the shader note in 3rd/trspk/es3/es3_shaders.h. */
    program = cutout ? &renderer->program_world_fast_cutout : &renderer->program_world_fast_plain;
    es3_use_program(renderer, program);
    es3_bind_texture0(renderer, renderer->atlas_texture);
}

bool
es3_upload_geometry(struct TRSPK_Renderer_ES3* renderer)
{
    uint32_t group;
    assert(renderer);
    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
        if( !es3_upload_group(renderer, &renderer->groups[group]) )
            return false;
    return es3_upload_dirty_static_batches(renderer);
}

/* ---- the draw sequence and the two per-frame rings -------------------------------- */

void
es3_sequence_reset(struct TRSPK_Renderer_ES3* renderer)
{
    assert(renderer);
    renderer->draw_item_count = 0u;
    renderer->ibo_staging_count = 0u;
    renderer->frame_stream_count = 0u;
}

static struct ES3DrawItem*
es3_sequence_append(struct TRSPK_Renderer_ES3* renderer)
{
    if( renderer->draw_item_count >= renderer->draw_item_capacity )
    {
        uint32_t capacity = renderer->draw_item_capacity ? renderer->draw_item_capacity * 2u
                                                         : ES3_DRAW_ITEM_INIT;
        struct ES3DrawItem* grown = (struct ES3DrawItem*)realloc(
            renderer->draw_items, (size_t)capacity * sizeof(*grown));
        assert(grown);
        renderer->draw_items = grown;
        renderer->draw_item_capacity = capacity;
    }
    return &renderer->draw_items[renderer->draw_item_count++];
}

void
es3_sequence_push_indexed(
    struct TRSPK_Renderer_ES3* renderer,
    uint32_t binding,
    uint32_t index_min,
    uint32_t index_max,
    bool cutout,
    bool blended,
    const uint32_t* indices,
    uint32_t index_count)
{
    uint32_t* destination;
    assert(renderer);
    assert(indices);
    if( index_count == 0u )
        return;
    destination = es3_sequence_reserve_indexed(renderer, index_count);
    memcpy(destination, indices, (size_t)index_count * sizeof(*indices));
    es3_sequence_commit_indexed(
        renderer, binding, index_min, index_max, cutout, blended, index_count);
}

uint32_t*
es3_sequence_reserve_indexed(
    struct TRSPK_Renderer_ES3* renderer,
    uint32_t index_count)
{
    uint32_t needed;
    assert(renderer);
    needed = renderer->ibo_staging_count + index_count;
    if( needed > renderer->ibo_staging_capacity )
    {
        uint32_t capacity = renderer->ibo_staging_capacity ? renderer->ibo_staging_capacity
                                                           : ES3_GPU_BUFFER_INIT;
        uint32_t* grown;
        while( capacity < needed )
            capacity *= 2u;
        grown = (uint32_t*)realloc(renderer->ibo_staging, (size_t)capacity * sizeof(*grown));
        assert(grown);
        renderer->ibo_staging = grown;
        renderer->ibo_staging_capacity = capacity;
    }
    return renderer->ibo_staging + renderer->ibo_staging_count;
}

void
es3_sequence_commit_indexed(
    struct TRSPK_Renderer_ES3* renderer,
    uint32_t binding,
    uint32_t index_min,
    uint32_t index_max,
    bool cutout,
    bool blended,
    uint32_t index_count)
{
    struct ES3DrawItem* item;
    uint32_t needed;
    assert(renderer);
    assert(index_min <= index_max);
    if( index_count == 0u )
        return;
    needed = renderer->ibo_staging_count + index_count;
    assert(needed <= renderer->ibo_staging_capacity);
    /* Merge with the item before it when nothing about the draw changed.
     * There is no page to compare: an index reaches the whole buffer, so
     * two indexed runs into the same binding and program are one draw
     * however far apart in the buffer their geometry lies. The merged item
     * covers the union of the two vertex ranges. */
    item = renderer->draw_item_count ? &renderer->draw_items[renderer->draw_item_count - 1u]
                                     : NULL;
    if( item && item->indexed && item->binding == binding && item->cutout == (uint8_t)cutout &&
        item->blended == (uint8_t)blended &&
        item->first + item->count == renderer->ibo_staging_count )
    {
        item->count += index_count;
        if( index_min < item->index_min )
            item->index_min = index_min;
        if( index_max > item->index_max )
            item->index_max = index_max;
    }
    else
    {
        item = es3_sequence_append(renderer);
        item->binding = binding;
        item->index_min = index_min;
        item->index_max = index_max;
        item->first = renderer->ibo_staging_count;
        item->count = index_count;
        item->indexed = 1u;
        item->cutout = (uint8_t)cutout;
        item->blended = (uint8_t)blended;
    }
    renderer->ibo_staging_count = needed;
}

void
es3_sequence_push_array(
    struct TRSPK_Renderer_ES3* renderer,
    uint32_t binding,
    uint32_t first,
    uint32_t count,
    bool cutout,
    bool blended)
{
    struct ES3DrawItem* item;
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
    item = es3_sequence_append(renderer);
    item->binding = binding;
    item->index_min = 0u;
    item->index_max = 0u;
    item->first = first;
    item->count = count;
    item->indexed = 0u;
    item->cutout = (uint8_t)cutout;
    item->blended = (uint8_t)blended;
}

uint32_t
es3_frame_stream_reserve(
    struct TRSPK_Renderer_ES3* renderer,
    uint32_t vertex_count)
{
    uint32_t first;
    assert(renderer);
    assert(renderer->frame_stream_cpu);
    first = renderer->frame_stream_count;
    trspk_vbo_ensure_capacity(renderer->frame_stream_cpu, first + vertex_count);
    renderer->frame_stream_count = first + vertex_count;
    return first;
}

void
es3_frame_stream_upload(struct TRSPK_Renderer_ES3* renderer)
{
    uint32_t bytes;
    uint32_t offset;
    if( renderer->frame_stream_count == 0u )
        return;
    bytes = renderer->frame_stream_count * (uint32_t)sizeof(struct TRSPK_VertexGLES2);
    offset = es3_stream_set_append(
        &renderer->frame_stream,
        renderer->frame_slot,
        GL_ARRAY_BUFFER,
        ES3_FRAME_STREAM_INIT_BYTES,
        renderer->frame_stream_cpu->vertices.as_gles2,
        bytes,
        false);
    renderer->frame_stream_vbo = renderer->frame_stream.buffers[renderer->frame_slot];
    renderer->bound_array_buffer = renderer->frame_stream_vbo;
    renderer->frame_stream_gpu_base = offset / (uint32_t)sizeof(struct TRSPK_VertexGLES2);
    /* The stream just moved. es3_bind_stream compares the binding's base
     * against what its VAO holds, so nothing has to be invalidated by hand
     * -- but the stream cache is kept truthful for the debug readouts. */
    if( renderer->stream_buffer == renderer->frame_stream_vbo )
        renderer->stream_layout = ES3_STREAM_NONE;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DYNAMIC_VBO_UPLOAD_BYTES, (int64_t)bytes);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DYNAMIC_VBO_UPLOADS, 1);
}



/* ---- the 3D pass ------------------------------------------------------------------- */

void
es3_mat4_multiply(
    const float* a,
    const float* b,
    float* out)
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



void
es3_set_letterbox_viewport(struct TRSPK_Renderer_ES3* renderer)
{
    glViewport(
        renderer->letterbox_x,
        renderer->letterbox_y,
        renderer->letterbox_width,
        renderer->letterbox_height);
}


/* ---- batch commands --------------------------------------------------------------- */



void
es3_batch_end(
    struct TRSPK_Renderer_ES3* renderer,
    const struct ToriRS_RenderCommand_Batch* command)
{
    struct ES3StaticBatch* batch;
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
    (void)es3_static_batch_commit(renderer, (uint32_t)slot);
    renderer->current_batch_slot = -1;
}


/* ---- dispatch ---------------------------------------------------------------------- */


/* ---- lifetime ----------------------------------------------------------------------- */

/* A lever's environment switch: unset or anything but "0" is on. */
/* A lever that is OFF unless NAME is set to something other than 0. The
 * counterpart of es3_lever_enabled, which is on unless NAME is 0.
 *
 * Only DIAGNOSTICS use this now. Every PERFORMANCE lever moved to
 * es3_lever_enabled, which is what the ES 2.0 core has always used -- it has
 * no opt_in at all, and its optimised bake, its NEON triplet packer and its
 * resident ring are simply on.
 *
 * They were opt-in because this core was written as the browser's, where the
 * cautious default costs little. On Android that is backwards: this is a
 * 2013 phone whose frame is CPU-bound, the specialised encoders exist for
 * exactly that, and shipping with them off meant --gles3 ran the generic
 * path while --gles2 ran the tuned one. A shared core may not mean the
 * slower lane's defaults. @see TORIRS_BAKE_VERIFY, which proves the fast
 * encoders byte-identical to the generic writer. */
static bool
es3_lever_opt_in(const char* name)
{
    const char* value = getenv(name);
    return value && value[0] != '0';
}

/*
 * A lever whose default follows the HARDWARE, not the core.
 *
 * On where the specialised encoders this core calls are actually specialised
 * -- armv7 with NEON, which is the Android lane -- and opt-in everywhere
 * else, which is wasm. This is what the ES 2.0 core does for the same five
 * levers, and it is the whole point of ES and WebGL being separate lanes over
 * one core: the browser's ceiling must not set the phone's defaults.
 *
 * Getting this wrong is silent and expensive. These shipped as plain opt-ins,
 * so --gles3 ran the generic bake while --gles2 ran the tuned one on the same
 * device, and nothing said so.
 */
static bool
es3_lever_neon_default(const char* name)
{
    const char* value = getenv(name);
#if defined(__arm__) && (defined(__ARM_NEON) || defined(__ARM_NEON__))
    return !(value && value[0] == '0' && value[1] == '\0');
#else
    return value && value[0] == '1';
#endif
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
static uint32_t g_es3_rotmask_source_generation = 1u;

void
TRSPK_Renderer_ES3_RotmaskSourceChanged(void)
{
    g_es3_rotmask_source_generation++;
    if( g_es3_rotmask_source_generation == 0u )
        g_es3_rotmask_source_generation = 1u; /* 0 is "never uploaded" in a slot */
}

uint32_t
es3_rotmask_source_generation(void)
{
    return g_es3_rotmask_source_generation;
}

struct TRSPK_Renderer_ES3*
TRSPK_Renderer_ES3_New(
    int width,
    int height,
    char const* name)
{
    struct TRSPK_Renderer_ES3* renderer;
    static uint8_t white_tile[TRSPK_ATLAS_TILE * TRSPK_ATLAS_TILE * 4u];
    uint32_t group;
    int texture;

    assert(width > 0);
    assert(height > 0);
    assert(name);
    renderer = (struct TRSPK_Renderer_ES3*)calloc(1u, sizeof(*renderer));
    assert(renderer);
    renderer->name = name;
    g_es3_name = name;
    renderer->width = width;
    renderer->height = height;
    renderer->interface_scale_mode = 2;
    renderer->tex_slot_next = 1u;
    renderer->current_batch_slot = -1;
    /* TORIRS_ES3_DEBUG=1: the 300-frame counters and the debug-only GL
     * error checks. Read once here; it used to be a getenv in es3_end_3d. */
    renderer->debug = getenv("TORIRS_ES3_DEBUG") != NULL;
    /* The levers (see the struct): each defaults ON; NAME=0 is the control
     * arm. Read once, here, so no frame ever scans the environment. */
    /*
     * Four CPU-side arms the GLES2 renderer carries, each measured on the
     * 2013 phone it was written for and each defaulting OFF anywhere else --
     * which includes this lane. They are opt-in by name here for the same
     * reason: a browser is not that phone, and a default nobody measured is
     * not a default. The ARM-only enable of the shared renderer collapses to
     * the off arm at compile time on wasm, so it is spelled out rather than
     * carried as a dead preprocessor branch.
     */
    renderer->draw_audit = es3_lever_opt_in("TORIRS_ES3_DRAW_AUDIT");

    renderer->actor_world_cache_enabled = es3_lever_neon_default("TORIRS_ES3_ACTOR_WORLD_CACHE");
    for( texture = 0; texture < TORIDRAW_TEXTURE_ID_CAPACITY; texture++ )
        renderer->tex_slot_of_id[texture] = -1;
    trspk_pose_table_init(&renderer->poses);
    trspk_pose_table_init(&renderer->batch_poses);
    renderer->frame_stream_cpu = trspk_vbo_create(0u, TRSPK_VERTEX_FORMAT_GLES2);
    assert(renderer->frame_stream_cpu);
    if( !trspk_atlas_init_grid(
            &renderer->atlas,
            ES3_ATLAS_DIM,
            ES3_ATLAS_DIM,
            TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE,
            4u) )
    {
        TRSPK_Renderer_ES3_Free(renderer);
        return NULL;
    }
    memset(white_tile, 0xff, sizeof(white_tile));
    if( !trspk_atlas_grid_insert_at(
            &renderer->atlas,
            0u,
            white_tile,
            TRSPK_ATLAS_TILE * 4u,
            TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE,
            NULL) )
    {
        TRSPK_Renderer_ES3_Free(renderer);
        return NULL;
    }
    renderer->tex_resident[0] = 1u;
    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
    {
        struct ES3ModelGroup* model_group = &renderer->groups[group];
        model_group->vbo_cpu = trspk_vbo_create(0u, TRSPK_VERTEX_FORMAT_GLES2);
        assert(model_group->vbo_cpu);
        model_group->arena = trspk_modelarena_create(
            model_group->vbo_cpu, &model_group->triangles, ES3_VBO_PAGE, 64u);
        assert(model_group->arena);
        model_group->reset_each_frame = group == TRSPK_VBO_GROUP_DYNAMIC;
    }
    es3_ui_init_state(renderer);
    return renderer;
}

static uint64_t
es3_pose_table_bytes(const struct TRSPK_PoseTable* table)
{
    uint64_t bytes = (uint64_t)table->element_cap * sizeof(struct TRSPK_PoseElement);
    uint32_t element_index;
    uint32_t track;
    for( element_index = 0u; element_index < table->element_count; element_index++ )
        for( track = 0u; track < TRSPK_POSE_TRACK_COUNT; track++ )
            bytes +=
                (uint64_t)table->elements[element_index].tracks[track].pose_cap * sizeof(uint32_t);
    return bytes;
}

/* One-shot shutdown attribution of every retained pool the renderer owns,
 * the peer of d3d9_report_retained_memory. GL buffer sizes are what was
 * asked for; the driver's own copy is not visible from here. */
static void
es3_report_retained_memory(struct TRSPK_Renderer_ES3* renderer)
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
        const struct ES3ModelGroup* model_group = &renderer->groups[group];
        group_vbo_cpu[group] = model_group->vbo_cpu ? (uint64_t)model_group->vbo_cpu->capacity *
                                                          sizeof(struct TRSPK_VertexGLES2)
                                                    : 0u;
        group_tri_cpu[group] = (uint64_t)model_group->triangles.cap * sizeof(int);
        group_slots_cpu[group] = model_group->arena ? (uint64_t)model_group->arena->slot_capacity *
                                                          sizeof(struct TRSPK_ModelSlot)
                                                    : 0u;
        group_vbo_gpu[group] =
            (uint64_t)model_group->gpu_capacity * sizeof(struct TRSPK_VertexGLES2);
    }
    pose_table_megabytes = ((double)es3_pose_table_bytes(&renderer->poses) +
                            (double)es3_pose_table_bytes(&renderer->batch_poses)) /
                           1048576.0;
    /* TORIRS_LOG compiles out of a release build; the figure is still computed
     * so the function that produces it is not dead code there. */
    (void)pose_table_megabytes;
    TORIRS_LOG(
        "es3_mem: === retained memory report ===\n"
        "es3_mem: batch16_cpu_vertices  %10.2f MB (%u chunks)\n"
        "es3_mem: batch16_cpu_configs   %10.2f MB\n"
        "es3_mem: static_pages_gpu      %10.2f MB (%u pages)\n"
        "es3_mem: group_static_cpu      %10.2f MB (vbo) + %.2f MB (configs) + %.2f MB (slots)\n"
        "es3_mem: group_static_gpu      %10.2f MB\n"
        "es3_mem: group_dynamic_cpu     %10.2f MB (vbo) + %.2f MB (configs)\n"
        "es3_mem: group_dynamic_gpu     %10.2f MB\n"
        "es3_mem: index_stream_gpu      %10.2f MB (one of %u)\n"
        "es3_mem: frame_stream_gpu      %10.2f MB (one of %u) + %.2f MB (cpu)\n"
        "es3_mem: draw_items_cpu        %10.2f MB\n"
        "es3_mem: ibo_staging_cpu       %10.2f MB\n"
        "es3_mem: model_indices_cpu     %10.2f MB\n"
        "es3_mem: atlas_cpu             %10.2f MB world + %.2f MB ui\n"
        "es3_mem: pose_tables_cpu       %10.2f MB\n",
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
        ES3_FRAMES_IN_FLIGHT,
        (double)renderer->frame_stream.capacities[renderer->frame_slot] / 1048576.0,
        ES3_FRAMES_IN_FLIGHT,
        renderer->frame_stream_cpu ? (double)renderer->frame_stream_cpu->capacity *
                                         sizeof(struct TRSPK_VertexGLES2) / 1048576.0
                                   : 0.0,
        (double)renderer->draw_item_capacity * sizeof(struct ES3DrawItem) / 1048576.0,
        (double)renderer->ibo_staging_capacity * sizeof(uint16_t) / 1048576.0,
        (double)renderer->model_index_capacity * sizeof(uint16_t) / 1048576.0,
        (double)renderer->atlas.stride * renderer->atlas.height / 1048576.0,
        (double)renderer->ui_sprite_atlas.stride * renderer->ui_sprite_atlas.height / 1048576.0,
        pose_table_megabytes);
    es3_ui_report_memory(renderer);
    es3_zbuffer_report_memory(renderer);
}

/* ---- client scaling's offscreen target --------------------------------------- */

void
es3_scale_target_destroy_buffers(struct TRSPK_Renderer_ES3* renderer)
{
    assert(renderer);
    if( renderer->scale_fbo )
        glDeleteFramebuffers(1, &renderer->scale_fbo);
    if( renderer->scale_texture )
    {
        /* Deleting a bound texture reverts the unit to 0; the cache follows. */
        if( renderer->bound_texture0 == renderer->scale_texture )
            renderer->bound_texture0 = 0u;
        glDeleteTextures(1, &renderer->scale_texture);
    }
    if( renderer->scale_depth )
        glDeleteRenderbuffers(1, &renderer->scale_depth);
    renderer->scale_fbo = 0u;
    renderer->scale_texture = 0u;
    renderer->scale_depth = 0u;
    renderer->scale_fbo_width = 0;
    renderer->scale_fbo_height = 0;
    renderer->scale_texture_filter = 0;
}

/*
 * The full-screen quad the present and the interface composite draw with,
 * in its own VAO. Made once; after that a present is one bind.
 */
void
es3_bind_present_quad(struct TRSPK_Renderer_ES3* renderer)
{
    const GLsizei stride = (GLsizei)sizeof(struct ES3VertexUI);

    assert(renderer);
    if( !renderer->vao_present )
    {
        /* Two triangles over clip space; v = 0 is the texture's bottom row,
         * which is GL's bottom row of the frame too. */
        static const float corners[6][4] = {
            { -1.0f, -1.0f, 0.0f, 0.0f },
            { 1.0f,  -1.0f, 1.0f, 0.0f },
            { 1.0f,  1.0f,  1.0f, 1.0f },
            { -1.0f, -1.0f, 0.0f, 0.0f },
            { 1.0f,  1.0f,  1.0f, 1.0f },
            { -1.0f, 1.0f,  0.0f, 1.0f },
        };
        struct ES3VertexUI vertices[6];
        int i;
        for( i = 0; i < 6; i++ )
        {
            vertices[i].x = corners[i][0];
            vertices[i].y = corners[i][1];
            vertices[i].w = 1.0f;
            vertices[i].u = corners[i][2];
            vertices[i].v = corners[i][3];
            vertices[i].rgba = 0xffffffffu;
            vertices[i].sel = 0.0f;
        }
        glGenVertexArrays(1, &renderer->vao_present);
        assert(renderer->vao_present);
        glGenBuffers(1, &renderer->present_vbo);
        assert(renderer->present_vbo);
        es3_bind_vao(renderer, renderer->vao_present);
        glBindBuffer(GL_ARRAY_BUFFER, renderer->present_vbo);
        renderer->bound_array_buffer = renderer->present_vbo;
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(vertices), vertices, GL_STATIC_DRAW);
        /* Every enabled array points at valid data, used by the program or
         * not: a stray enabled array drops the draw on some drivers. */
        glEnableVertexAttribArray(ES3_ATTRIB_POSITION);
        glVertexAttribPointer(
            ES3_ATTRIB_POSITION,
            3,
            GL_FLOAT,
            GL_FALSE,
            stride,
            (const void*)(uintptr_t)offsetof(struct ES3VertexUI, x));
        glEnableVertexAttribArray(ES3_ATTRIB_TEXCOORD);
        glVertexAttribPointer(
            ES3_ATTRIB_TEXCOORD,
            2,
            GL_FLOAT,
            GL_FALSE,
            stride,
            (const void*)(uintptr_t)offsetof(struct ES3VertexUI, u));
        glEnableVertexAttribArray(ES3_ATTRIB_COLOR);
        glVertexAttribPointer(
            ES3_ATTRIB_COLOR,
            4,
            GL_UNSIGNED_BYTE,
            GL_TRUE,
            stride,
            (const void*)(uintptr_t)offsetof(struct ES3VertexUI, rgba));
        glEnableVertexAttribArray(ES3_ATTRIB_TEXINFO);
        glVertexAttribPointer(
            ES3_ATTRIB_TEXINFO,
            1,
            GL_FLOAT,
            GL_FALSE,
            stride,
            (const void*)(uintptr_t)offsetof(struct ES3VertexUI, sel));
    }
    es3_bind_vao(renderer, renderer->vao_present);
    renderer->stream_buffer = renderer->present_vbo;
    renderer->stream_byte_offset = 0u;
    renderer->stream_layout = ES3_STREAM_NONE;
}

static void
es3_scale_target_destroy(struct TRSPK_Renderer_ES3* renderer)
{
    assert(renderer);
    es3_scale_target_destroy_buffers(renderer);
    if( renderer->vao_present )
    {
        if( renderer->vao_bound == renderer->vao_present )
        {
            glBindVertexArray(0);
            renderer->vao_bound = 0u;
        }
        glDeleteVertexArrays(1, &renderer->vao_present);
    }
    if( renderer->present_vbo )
    {
        if( renderer->bound_array_buffer == renderer->present_vbo )
            renderer->bound_array_buffer = 0u;
        if( renderer->stream_buffer == renderer->present_vbo )
            renderer->stream_layout = ES3_STREAM_NONE;
        glDeleteBuffers(1, &renderer->present_vbo);
    }
    renderer->vao_present = 0u;
    renderer->present_vbo = 0u;
}

/* The offscreen target at this frame's render size, (re)made only when the
 * size changes. Colour is an RGBA texture with no mipmaps and clamped edges,
 * which WebGL1 accepts at any size. Colour ALONE: a renderer that needs
 * depth here follows with es3_scale_target_ensure_depth. */
void
es3_scale_target_ensure(struct TRSPK_Renderer_ES3* renderer)
{
    GLint filter;
    GLenum status;

    assert(renderer);
    assert(renderer->target_width > 0);
    assert(renderer->target_height > 0);
    filter = renderer->client_scale.output_filter == CLIENT_SCALE_FILTER_NEAREST ? GL_NEAREST
                                                                                 : GL_LINEAR;
    if( renderer->scale_fbo && renderer->scale_fbo_width == renderer->target_width &&
        renderer->scale_fbo_height == renderer->target_height )
    {
        if( renderer->scale_texture_filter != filter )
        {
            es3_bind_texture0(renderer, renderer->scale_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            renderer->scale_texture_filter = filter;
            /* Never left bound while the frame draws into it. */
            es3_bind_texture0(renderer, 0u);
        }
        return;
    }

    es3_scale_target_destroy_buffers(renderer);
    glGenTextures(1, &renderer->scale_texture);
    assert(renderer->scale_texture);
    es3_bind_texture0(renderer, renderer->scale_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        renderer->target_width,
        renderer->target_height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        NULL);
    es3_bind_texture0(renderer, 0u);
    renderer->scale_texture_filter = filter;

    glGenFramebuffers(1, &renderer->scale_fbo);
    assert(renderer->scale_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->scale_fbo);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderer->scale_texture, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if( status != GL_FRAMEBUFFER_COMPLETE )
        TORIRS_ERR(
            "%s: offscreen target %dx%d incomplete: 0x%x\n",
            g_es3_name,
            renderer->target_width,
            renderer->target_height,
            (unsigned)status);
    assert(status == GL_FRAMEBUFFER_COMPLETE);
    renderer->scale_fbo_width = renderer->target_width;
    renderer->scale_fbo_height = renderer->target_height;
}

/*
 * The depth renderbuffer for the offscreen target, attached to whatever
 * es3_scale_target_ensure last built. Idempotent: the colour rebuild is what
 * drops it, so a live ::scale_depth is already the right size.
 */
void
es3_scale_target_ensure_depth(struct TRSPK_Renderer_ES3* renderer)
{
    GLenum status;

    assert(renderer);
    assert(renderer->scale_fbo);
    if( renderer->scale_depth )
        return;
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->scale_fbo);
    glGenRenderbuffers(1, &renderer->scale_depth);
    assert(renderer->scale_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, renderer->scale_depth);
    /* 24-bit, which is what the window itself is asked for. ES2 only
     * guarantees GL_DEPTH_COMPONENT16 as a renderbuffer format, so the
     * GLES2 renderer's offscreen path is a bit shallower than its direct
     * one; ES3 guarantees 24 and the two match here. */
    glRenderbufferStorage(
        GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, renderer->target_width, renderer->target_height);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderer->scale_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if( status != GL_FRAMEBUFFER_COMPLETE )
        TORIRS_ERR(
            "%s: offscreen depth %dx%d incomplete: 0x%x\n",
            g_es3_name,
            renderer->target_width,
            renderer->target_height,
            (unsigned)status);
    assert(status == GL_FRAMEBUFFER_COMPLETE);
}

/* The finished offscreen frame onto the output rect of the drawable, with
 * the bars cleared black. WebGL2 has no blit: one textured quad. */
/*
 * The finished offscreen frame onto the output rect of the drawable, with
 * the bars cleared black.
 *
 * Still a textured quad rather than a glBlitFramebuffer, on purpose: the
 * present shader forces alpha to 1, because what is in the offscreen alpha
 * channel is blend residue and the canvas this lands on is composited by the
 * browser. A blit would carry that residue through and show the page behind
 * the client.
 *
 * What ES3 does buy here is the end of it: once the quad has been drawn the
 * offscreen's contents are finished with, and glInvalidateFramebuffer says
 * so. A tile-based GPU -- which is most of what a browser runs on now -- can
 * then skip writing those tiles back to memory entirely, and the next frame
 * has nothing to restore.
 */
void
es3_scale_target_present(struct TRSPK_Renderer_ES3* renderer)
{
    assert(renderer);
    assert(renderer->scale_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    es3_set_scissor(renderer, NULL);
    es3_set_blend(renderer, false);
    es3_set_cull(renderer, false);
    /* Depth write on so the clear reaches the depth buffer. */
    es3_set_depth(renderer, false, true);
    glViewport(0, 0, renderer->drawable_width, renderer->drawable_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(
        renderer->output_x, renderer->output_y, renderer->output_width, renderer->output_height);

    es3_use_program(renderer, &renderer->program_present);
    es3_bind_texture0(renderer, renderer->scale_texture);
    es3_bind_present_quad(renderer);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    {
        GLenum discard[2];
        GLsizei count = 0;
        discard[count++] = GL_COLOR_ATTACHMENT0;
        if( renderer->scale_depth )
            discard[count++] = GL_DEPTH_ATTACHMENT;
        /* The texture is still bound on unit 0 above; the invalidate names
         * the FRAMEBUFFER's attachments, so the binding has to move first. */
        es3_bind_texture0(renderer, 0u);
        glBindFramebuffer(GL_FRAMEBUFFER, renderer->scale_fbo);
        glInvalidateFramebuffer(GL_FRAMEBUFFER, count, discard);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

static void
es3_destroy_gl_resources(struct TRSPK_Renderer_ES3* renderer)
{
    uint32_t group;
    assert(renderer);
    es3_ui_destroy_gl(renderer);
    /*
     * The vertex array objects, before the buffers they name.
     *
     * They must go, not merely be forgotten: a renderer switch frees this
     * renderer and may build another, GL recycles object names, and a VAO
     * that outlived its buffer would match the recycled name in the
     * (buffer, base) cache es3_bind_stream keeps and skip re-specifying
     * the layout -- a stream drawn from whatever the new buffer of that name
     * holds.
     */
    es3_bind_vao(renderer, 0u);
    for( group = 0u; group < ES3_BINDING_COUNT; group++ )
    {
        if( renderer->vao_world[group] )
            glDeleteVertexArrays(1, &renderer->vao_world[group]);
        renderer->vao_world[group] = 0u;
        renderer->vao_world_buffer[group] = 0u;
        renderer->vao_world_base[group] = 0u;
    }
    if( renderer->vao_ui )
        glDeleteVertexArrays(1, &renderer->vao_ui);
    if( renderer->vao_rotmask )
        glDeleteVertexArrays(1, &renderer->vao_rotmask);
    renderer->vao_ui = 0u;
    renderer->vao_ui_buffer = 0u;
    renderer->vao_ui_offset = 0u;
    renderer->vao_rotmask = 0u;
    renderer->vao_rotmask_buffer = 0u;
    renderer->vao_rotmask_offset = 0u;
    if( renderer->world_ubo )
        glDeleteBuffers(1, &renderer->world_ubo);
    renderer->world_ubo = 0u;
    es3_delete_program(&renderer->program_world_plain);
    es3_delete_program(&renderer->program_world_cutout);
    es3_delete_program(&renderer->program_world_fast_plain);
    es3_delete_program(&renderer->program_world_fast_cutout);
    es3_delete_program(&renderer->program_ui);
    es3_delete_program(&renderer->program_rotmask);
    es3_delete_program(&renderer->program_present);
    es3_delete_program(&renderer->program_ui_composite);
    es3_scale_target_destroy(renderer);
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
    es3_stream_set_destroy(&renderer->index_stream);
    es3_stream_set_destroy(&renderer->dynamic_stream);
    es3_stream_set_destroy(&renderer->frame_stream);
    es3_stream_set_destroy(&renderer->ui_stream);
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
TRSPK_Renderer_ES3_Free(struct TRSPK_Renderer_ES3* renderer)
{
    uint32_t batch;
    uint32_t group;
    if( !renderer )
        return;
    es3_report_retained_memory(renderer);
    if( renderer->gl_context )
    {
        ToriPlatform_GLContext_MakeCurrent(renderer->window, renderer->gl_context);
        es3_destroy_gl_resources(renderer);
    }
    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
    {
        if( renderer->groups[group].arena )
            trspk_modelarena_free(renderer->groups[group].arena);
        if( renderer->groups[group].vbo_cpu )
            trspk_vbo_free(renderer->groups[group].vbo_cpu);
        trspk_triangles_free(&renderer->groups[group].triangles);
    }
    free(renderer->actor_world_xyz);
    if( renderer->frame_stream_cpu )
        trspk_vbo_free(renderer->frame_stream_cpu);
    trspk_triangles_free(&renderer->frame_stream_triangles);
    free(renderer->draw_items);
    trspk_pose_table_free(&renderer->poses);
    trspk_pose_table_free(&renderer->batch_poses);
    free(renderer->static_primary);
    free(renderer->static_primary_bits);
    es3_zbuffer_destroy(renderer);
    for( batch = 0u; batch < renderer->static_batch_count; batch++ )
    {
        trspk_batch16_destroy(renderer->static_batches[batch].cpu);
        free(renderer->static_batches[batch].page_ids);
    }
    if( trspk_atlas_is_initialized(&renderer->atlas) )
        trspk_atlas_free(&renderer->atlas);
    es3_ui_free(renderer);
    free(renderer->upload_stage);
    free(renderer->ibo_staging);
    free(renderer->model_indices);
    free(renderer->static_pages);
    free(renderer->static_batches);
    if( renderer->gl_context )
        ToriPlatform_GLContext_Delete(renderer->gl_context);
    free(renderer);
}

/*
 * Everything a context needs that neither world path decides: the context
 * itself at the depth the CALLER asks for, the programs, the atlas, the UI
 * unit and the attribute state.
 *
 * `depth_bits` and `world_pass` are the whole of what the two composing
 * renderers differ by here, and both are data, not a test: the painter asks
 * for 0 bits and calls itself "painter", the depth renderer asks for 16 and
 * calls itself "depth-buffered". @see TRSPK_Renderer_ES3_PainterInit,
 * TRSPK_Renderer_ES3_ZBufferInit.
 */
bool
es3_init_gl(
    struct TRSPK_Renderer_ES3* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene,
    int depth_bits,
    char const* world_pass)
{
    GLint max_texture_size = 0;

    assert(renderer);
    assert(window);
    assert(scene);
    assert(world_pass);
    if( renderer->gl_context )
        return false;
    renderer->scene = scene;
    renderer->kernel = ToriDraw_KernelGetGpu();
    renderer->window = window;

    /* Depth is a CREATION attribute -- part of the EGL config -- which is why
     * it is a parameter of the create call. 16 bits: the format every WebGL2
     * device offers; EGL treats the request as a floor, so a device with more
     * may hand more back. */
    renderer->gl_context = ToriPlatform_GLContext_Create(window, depth_bits, TORIPLATFORM_GL_CLIENT_ES3);
    if( !renderer->gl_context )
    {
        TORIRS_ERR("%s: context creation failed: %s\n", g_es3_name, ToriPlatform_GLContext_LastError());
        return false;
    }
    ToriPlatform_GLContext_SetSwapInterval(0);

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    TORIRS_LOG(
        "%s: %s | GLSL %s | %s | max texture %d\n",
        g_es3_name,
        (const char*)glGetString(GL_VERSION),
        (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION),
        (const char*)glGetString(GL_RENDERER),
        (int)max_texture_size);
    if( max_texture_size < (GLint)ES3_ATLAS_DIM )
    {
        TORIRS_ERR(
            "%s: GL_MAX_TEXTURE_SIZE %d is below the %u atlas this renderer needs\n",
            g_es3_name,
            (int)max_texture_size,
            ES3_ATLAS_DIM);
        goto fail;
    }
    if( !es3_create_programs(renderer) )
        goto fail;

    glGenTextures(1, &renderer->atlas_texture);
    if( !es3_upload_atlas(renderer) )
        goto fail;
    if( !es3_ui_create_gl(renderer) )
        goto fail;

    /* The first four attributes are live for the life of the context (the
     * fourth is the world's texinfo and the UI's sampler select; the rotmask
     * layout keeps it pointed at something valid); the mask uv follows the
     * rotmask layout. */
    glEnableVertexAttribArray(ES3_ATTRIB_POSITION);
    glEnableVertexAttribArray(ES3_ATTRIB_TEXCOORD);
    glEnableVertexAttribArray(ES3_ATTRIB_COLOR);
    glEnableVertexAttribArray(ES3_ATTRIB_TEXINFO);
    es3_state_reset(renderer);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    if( !es3_check_error("init") )
        goto fail;

    if( renderer->static_page_count > 0u )
    {
        bool recreated = false;
        if( !es3_ensure_static_batch_vbo(renderer, renderer->static_page_count, &recreated) )
            goto fail;
        if( recreated )
            es3_mark_active_static_batches_dirty(renderer);
        if( !es3_upload_dirty_static_batches(renderer) )
            goto fail;
    }
    TORIRS_LOG("%s: renderer up (%s world pass)\n", g_es3_name, world_pass);
    return true;

fail:
    es3_destroy_gl_resources(renderer);
    ToriPlatform_GLContext_Delete(renderer->gl_context);
    renderer->gl_context = NULL;
    return false;
}

void
TRSPK_Renderer_ES3_SetViewport(
    struct TRSPK_Renderer_ES3* renderer,
    int width,
    int height)
{
    assert(renderer);
    if( width <= 0 || height <= 0 || (renderer->width == width && renderer->height == height) )
        return;
    renderer->width = width;
    renderer->height = height;
    es3_update_letterbox(renderer, renderer->target_offscreen);
    renderer->in2d = false;
    es3_ui_batch_reset(renderer);
}

void
TRSPK_Renderer_ES3_SetInterfaceScaleMode(
    struct TRSPK_Renderer_ES3* renderer,
    int mode)
{
    assert(renderer);
    /* Read at the next BEGIN_2D (es3_ui_layer_wanted); interface art is
     * always sampled nearest, so no texture is refiltered. */
    renderer->interface_scale_mode = es3_clampi(mode, 0, 2);
}

void
TRSPK_Renderer_ES3_SetClientScaling(
    struct TRSPK_Renderer_ES3* renderer,
    struct ClientScaleSettings const* settings)
{
    assert(renderer);
    assert(settings);
    renderer->client_scale = *settings;
}

void
TRSPK_Renderer_ES3_SetPick(
    struct TRSPK_Renderer_ES3* renderer,
    int mouse_x,
    int mouse_y)
{
    assert(renderer);
    renderer->pick_enabled = true;
    renderer->pick_mouse_x = mouse_x;
    renderer->pick_mouse_y = mouse_y;
    ToriRS_PickHitsReset(&renderer->pick_hits);
}

struct ToriRS_PickHits const*
TRSPK_Renderer_ES3_PickHits(struct TRSPK_Renderer_ES3 const* renderer)
{
    assert(renderer);
    return &renderer->pick_hits;
}

/*
 * TORIRS_ES3_READBACK=path dumps one finished frame, through the same
 * readback the app's screenshots use, so a bug in the letterbox arithmetic
 * cannot show in a debug dump and not in a screenshot. Both composing
 * renderers end their frame with it.
 */
void
es3_frame_readback(struct TRSPK_Renderer_ES3* renderer)
{
    assert(renderer);
/* TORIRS_ES3_READBACK=path dumps one frame (after
 * TORIRS_ES3_READBACK_FRAME, default 90) through the same readback the
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
        char const* frame = getenv("TORIRS_ES3_READBACK_FRAME");
        path = getenv("TORIRS_ES3_READBACK");
        if( frame )
            want = atol(frame);
        probed = 1;
    }
    if( path && path[0] && !done && renderer->frame_clock >= (double)want )
    {
        int* top =
            (int*)malloc((size_t)renderer->width * (size_t)renderer->height * sizeof(int));
        void bmp_write_file(const char* filename, int* px, int w, int h);
        done = 1;
        assert(top);
        if( TRSPK_Renderer_ES3_ReadPixels(renderer, top, renderer->width, renderer->height) )
        {
            bmp_write_file(path, top, renderer->width, renderer->height);
            TORIRS_LOG("es3_readback: wrote %s\n", path);
        }
        free(top);
    }
}
}


/* Bring the surface up for a frame: current, measured, letterboxed, cleared.
 * False when there is no surface to draw on (a stopped activity). */
/*
 * Bring the surface up for a frame: current, measured, letterboxed, state
 * reset. False when there is no surface to draw on (a stopped activity).
 *
 * The FRAMEBUFFER is deliberately not bound here. Whether the frame is drawn
 * offscreen, and whether that offscreen target carries depth, is the
 * composing renderer's to say -- @see es3p_begin_frame and es3z_begin_frame,
 * which bracket this with their own two lines and then call
 * es3_frame_surface_clear.
 */
bool
es3_frame_surface_begin(
    struct TRSPK_Renderer_ES3* renderer,
    bool allow_offscreen)
{
    assert(renderer);
    if( !renderer->gl_context )
        return false;
    if( ToriPlatform_GLContext_MakeCurrent(renderer->window, renderer->gl_context) != 0 )
        return false;
    ToriPlatform_GLContext_DrawableSize(
        renderer->window, &renderer->drawable_width, &renderer->drawable_height);
    if( renderer->drawable_width <= 0 || renderer->drawable_height <= 0 )
        return false;
    /* An interface layer is opened and composited inside one 2D segment. */
    assert(!renderer->ui_layer_open);
    es3_update_letterbox(renderer, allow_offscreen);
    es3_state_reset(renderer);
    es3_stream_sets_begin_frame(renderer);
    return true;
}

/* The other half: viewport, clear, letterbox fill. Runs once the composing
 * renderer has bound the framebuffer it drew its own conclusion about. */
void
es3_frame_surface_clear(
    struct TRSPK_Renderer_ES3* renderer,
    bool clear_to_black_only)
{
    struct ES3Rect letterbox;
    assert(renderer);
    glViewport(0, 0, renderer->target_width, renderer->target_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if( !clear_to_black_only && renderer->letterbox_width > 0 && renderer->letterbox_height > 0 )
    {
        letterbox.x = renderer->letterbox_x;
        letterbox.y = renderer->letterbox_y;
        letterbox.width = renderer->letterbox_width;
        letterbox.height = renderer->letterbox_height;
        es3_set_scissor(renderer, &letterbox);
        glClearColor(
            (float)((TORIRS_ES3_BG >> 16) & 0xffu) / 255.0f,
            (float)((TORIRS_ES3_BG >> 8) & 0xffu) / 255.0f,
            (float)(TORIRS_ES3_BG & 0xffu) / 255.0f,
            1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        es3_set_scissor(renderer, NULL);
    }
    es3_set_letterbox_viewport(renderer);
}

/* The bar's caption, through the same font path a frame uses, so a boot
 * sentence is one picture and not one per renderer. */
void
es3_draw_boot_caption(
    struct TRSPK_Renderer_ES3* renderer,
    int caption_font_id,
    char const* caption)
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
    es3_begin_2d(renderer);
    es3_ui_draw_font(renderer, &font_command);
    es3_end_2d(renderer);
}






/*
 * The frame that is about to be presented, sampled back onto the canvas grid.
 *
 * Two conversions: the frame is letterboxed inside the buffer it was drawn
 * into, and GL reports rows bottom-up while the client's buffers are top-down.
 * WebGL2 reads GL_RGBA only, so the bytes are repacked into the ARGB words the
 * rest of the client thinks in. An offscreen frame is read from its own
 * buffer, at render resolution, not from the scaled copy on the drawable.
 */
bool
TRSPK_Renderer_ES3_ReadPixels(
    struct TRSPK_Renderer_ES3* renderer,
    int* pixels,
    int width,
    int height)
{
    int framebuffer_w = 0;
    int framebuffer_h = 0;
    int read_x;
    int read_y;
    int read_w;
    int read_h;
    uint8_t* framebuffer;
    float scale_x;
    float scale_y;
    bool offscreen;
    int y;

    assert(renderer);
    assert(pixels);
    assert(width > 0);
    assert(height > 0);
    if( !renderer->gl_context || !renderer->window )
        return false;
    offscreen = renderer->target_offscreen && renderer->scale_fbo;
    if( offscreen )
    {
        framebuffer_w = renderer->scale_fbo_width;
        framebuffer_h = renderer->scale_fbo_height;
    }
    else
        ToriPlatform_GLContext_DrawableSize(renderer->window, &framebuffer_w, &framebuffer_h);
    if( framebuffer_w <= 0 || framebuffer_h <= 0 || renderer->letterbox_width <= 0 ||
        renderer->letterbox_height <= 0 )
        return false;
    /*
     * Only the letterbox is read, not the whole drawable. GL_PACK_ROW_LENGTH
     * would let the rows land inside a full-size buffer, but there is no
     * reason to want one: every sample below comes out of the letterbox, and
     * on a large window with a small canvas the bars are most of the pixels.
     * The ES2 renderer reads the lot because clipping the read without
     * GL_PACK_ROW_LENGTH means one glReadPixels per row.
     */
    read_x = es3_clampi(renderer->letterbox_x, 0, framebuffer_w - 1);
    read_y = es3_clampi(renderer->letterbox_y, 0, framebuffer_h - 1);
    read_w = es3_clampi(renderer->letterbox_width, 1, framebuffer_w - read_x);
    read_h = es3_clampi(renderer->letterbox_height, 1, framebuffer_h - read_y);
    framebuffer = (uint8_t*)malloc((size_t)read_w * (size_t)read_h * 4u);
    assert(framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    if( offscreen )
        glBindFramebuffer(GL_FRAMEBUFFER, renderer->scale_fbo);
    glReadPixels(read_x, read_y, read_w, read_h, GL_RGBA, GL_UNSIGNED_BYTE, framebuffer);
    if( offscreen )
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    scale_x = (float)read_w / (float)width;
    scale_y = (float)read_h / (float)height;
    for( y = 0; y < height; y++ )
    {
        /* GL reports rows bottom-up; the client's buffers are top-down. */
        int source_y = (int)((float)(height - 1 - y) * scale_y);
        int x;
        source_y = es3_clampi(source_y, 0, read_h - 1);
        for( x = 0; x < width; x++ )
        {
            int source_x = (int)((float)x * scale_x);
            const uint8_t* source;
            uint32_t alpha;
            source_x = es3_clampi(source_x, 0, read_w - 1);
            source = framebuffer + ((size_t)source_y * (size_t)read_w + (size_t)source_x) * 4u;
            /* The offscreen alpha channel is blend residue; the presented
             * picture is opaque (the present shader writes alpha 1). */
            alpha = offscreen ? 0xffu : (uint32_t)source[3];
            pixels[y * width + x] = (int)((alpha << 24) | ((uint32_t)source[0] << 16) |
                                          ((uint32_t)source[1] << 8) | (uint32_t)source[2]);
        }
    }
    free(framebuffer);
    return true;
}

char const*
TRSPK_Renderer_ES3_Name(struct TRSPK_Renderer_ES3 const* renderer)
{
    assert(renderer);
    return renderer->name;
}
