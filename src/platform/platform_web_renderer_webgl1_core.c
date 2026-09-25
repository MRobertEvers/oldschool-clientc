/**
 * The GLES2 renderer core: the context, the programs, the world texture atlas,
 * every retained CPU/GPU vertex buffer, the per-frame index stream and the
 * command dispatch.
 *
 * What it deliberately does not know is how the world's triangles get ordered.
 * It does not know it in the strong sense: there is no test anywhere in this
 * file for which of the two world paths is running. Each is a WHOLE renderer
 * that composes the toolkit below, and they share nothing with each other:
 *
 *   platform_web_renderer_webgl1_painter.c   painter's algorithm
 *   platform_web_renderer_webgl1_zbuffer.c   hardware depth test
 *
 * The caller picks one by calling its Init, and keeps calling that one's
 * Execute, DrawBootBar and RenderFrame. ::zbuffer is the depth renderer's
 * own state, not a selector anything reads.
 * See 3rd/trspk/es2/es2_core.h for the contract and for what the
 * GLES2 ceiling turned into here.
 *
 * The retained model is the D3D9 renderer's, kept on purpose (see
 * platform_win32_renderer_d3d9_core.c): two arena groups (STATIC, retained
 * across frames; DYNAMIC, refilled every frame for actors), plus Batch16 for
 * the scene build, whose pages are the pages the U16 index stream addresses.
 */

#include "es2/es2_core.h"
#include "platform/platform_web_renderer_webgl1_placement.h"

#include "engine/boot_bar.h"
#include "log/torirs_log.h"
#include "perf/torirs_perf.h"
#include "es2/es2_shaders.h"

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

/* ---- this family's specialisations ------------------------------------------------ */

/*
 * WEB (wasm). The webgl1 counterparts. Portable C only.
 */

void
es2_apply_animation(
    struct TRSPK_Renderer_ES2* renderer,
    const struct ToriRS_RenderCommand_Model* command)
{
    assert(renderer);
    assert(command);
    /* Re-applied every frame. The resolved path's saving is a phone's to
     * take; here the pose table it leans on is not the thing under pressure,
     * and this keeps the browser build free of a decision it cannot make. */
    ToriDraw_SceneElementApplyAnimation(
        renderer->scene, command->element_id, command->anim_index == 0, command->anim_frame);
}

static bool
es2_bake_ordered_fast(
    struct TRSPK_VBO* vbo,
    uint32_t vertex_base,
    const struct ToriDraw_Model* full_model,
    const int* face_order,
    uint32_t written_count,
    const float* world_xyz,
    bool ordered_painter)
{
    /*
     * No fast encoder here, so the core's generic loop does every actor.
     *
     * Declining in one place is the point: the packed word encoder is written
     * for armv7 and this lane never calls it, so nothing in the browser build
     * has to carry a test for whether it should.
     */
    (void)vbo;
    (void)vertex_base;
    (void)full_model;
    (void)face_order;
    (void)written_count;
    (void)world_xyz;
    (void)ordered_painter;
    return false;
}


#if defined(TORIRS_BAKE_CHAIN_CAPTURE)
#include "es2/es2_bake_capture.u.c"
#elif defined(TORIRS_BAKE_VERIFY)
#include "../../tools/perf/bake_chain_format.h"
#endif

#if defined(TORIRS_PLACEMENT_CAPTURE)
#include "es2/es2_placement_capture.u.c"
#endif



#if defined(TORIRS_MODEL_CHAIN_CAPTURE)
#include "es2/es2_chain_capture.u.c"
#endif

_Static_assert(
    ES2_ATLAS_COLS * TRSPK_ATLAS_TILE == ES2_ATLAS_DIM,
    "the atlas grid must tile the atlas exactly");
_Static_assert(
    ES2_ATLAS_COLS * ES2_ATLAS_COLS == ES2_ATLAS_SLOTS,
    "the slot count is the grid squared");
_Static_assert(
    sizeof(struct TRSPK_VertexGLES2) == 28u,
    "the world vertex layout is what the attribute pointers describe");
_Static_assert(
    sizeof(struct ES2VertexUI) == 28u,
    "the UI vertex layout is what the attribute pointers describe");
_Static_assert(
    sizeof(struct ES2VertexRotmask) == 32u,
    "the rotmask vertex layout is what the attribute pointers describe");
_Static_assert(
    ES2_ATTRIB_TEXINFO == ES2_ATTRIB_MASK_TEXCOORD,
    "the fourth attribute slot is shared: world texinfo or rotmask mask uv");

enum ES2StreamLayout
{
    ES2_STREAM_NONE = 0,
    ES2_STREAM_WORLD = 1,
    ES2_STREAM_UI = 2,
    ES2_STREAM_ROTMASK = 3,
};

#define ES2_VERTEX_STRIDE ((GLsizei)sizeof(struct TRSPK_VertexGLES2))

/* ---- cached GL state ------------------------------------------------------ */

void
es2_set_blend(struct TRSPK_Renderer_ES2* renderer, bool enabled)
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
es2_set_depth(struct TRSPK_Renderer_ES2* renderer, bool test, bool write)
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
es2_set_cull(struct TRSPK_Renderer_ES2* renderer, bool enabled)
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
es2_set_scissor(struct TRSPK_Renderer_ES2* renderer, const struct ES2Rect* rect)
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
es2_bind_texture0(struct TRSPK_Renderer_ES2* renderer, GLuint texture)
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
 * batch entry read alone was 39% of es2_dispatch.
 */
void
es2_prefetch_ahead_ids(struct TRSPK_Renderer_ES2* renderer,int id_plus1,int id_plus2,int id_plus3)
{
#if defined(TORIRS_PLACEMENT_CAPTURE)
    es2_placement_prefetch_record(renderer,id_plus1,id_plus2,id_plus3);
#endif
    es2_static_prefetch_ids(renderer,id_plus1,id_plus2,id_plus3);
}

void
es2_prefetch_ahead(
    struct TRSPK_Renderer_ES2* renderer,
    const struct ToriRS_Frame* frame)
{
    assert(renderer);
    assert(frame);
    if( !renderer->batch_poses.elements || !renderer->has_3d )
        return;
    es2_prefetch_ahead_ids(
        renderer,
        ToriRS_FrameLookaheadElementId(frame, 1),
        ToriRS_FrameLookaheadElementId(frame, 2),
        ToriRS_FrameLookaheadElementId(frame, 3));
}

void
es2_bind_texture1(struct TRSPK_Renderer_ES2* renderer, GLuint texture)
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
es2_use_program(struct TRSPK_Renderer_ES2* renderer, const struct ES2Program* program)
{
    assert(renderer);
    assert(program);
    if( renderer->current_program == program )
        return;
    glUseProgram(program->id);
    renderer->current_program = program;
}

void
es2_bind_array_buffer(struct TRSPK_Renderer_ES2* renderer, GLuint buffer)
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
es2_blend_func_default(void)
{
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void
es2_state_reset(struct TRSPK_Renderer_ES2* renderer)
{
    glDisable(GL_BLEND);
    es2_blend_func_default();
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
    renderer->stream_layout = ES2_STREAM_NONE;
}

/* ---- programs --------------------------------------------------------------- */

/*
 * What this renderer calls itself, for every line the core logs.
 *
 * A file static rather than a field, because six of the places that log --
 * the shader compiler, the program linker, the GL error drain -- have no
 * renderer in scope and threading one through them would be plumbing for a
 * string that cannot vary: main.c starts at most one GPU renderer at a time
 * (renderer_start asserts it) and the value is the LANE's, fixed for the
 * build. TRSPK_Renderer_ES2_New sets it; it is a literal owned by the lane file.
 */
static char const* g_es2_name = "ES2";

char const*
es2_log_name(void)
{
    return g_es2_name;
}

bool
es2_check_error(const char* where)
{
    GLenum error = glGetError();
    if( error == GL_NO_ERROR )
        return true;
    TORIRS_ERR("%s: %s: glGetError 0x%x\n", g_es2_name, where, (unsigned)error);
    return false;
}

static GLuint
es2_compile_shader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;
    if( shader == 0u )
    {
        TORIRS_ERR("%s: glCreateShader failed\n", g_es2_name);
        return 0u;
    }
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char log[1024];
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        TORIRS_ERR("%s: shader compile failed: %s\n", g_es2_name, log);
        glDeleteShader(shader);
        return 0u;
    }
    return shader;
}

static bool
es2_link_program(
    struct ES2Program* program,
    const char* vertex_source,
    const char* fragment_source,
    bool has_mask_attribute,
    bool has_texinfo_attribute,
    const char* label)
{
    GLuint vertex_shader = es2_compile_shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment_shader = es2_compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    GLint ok = 0;

    memset(program, 0, sizeof(*program));
    if( vertex_shader == 0u || fragment_shader == 0u )
    {
        if( vertex_shader )
            glDeleteShader(vertex_shader);
        if( fragment_shader )
            glDeleteShader(fragment_shader);
        TORIRS_ERR("%s: %s: shaders did not compile\n", g_es2_name, label);
        return false;
    }
    program->id = glCreateProgram();
    if( program->id == 0u )
    {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        TORIRS_ERR("%s: %s: glCreateProgram failed\n", g_es2_name, label);
        return false;
    }
    glAttachShader(program->id, vertex_shader);
    glAttachShader(program->id, fragment_shader);
    /* Before the link, so every program agrees on where each attribute
     * lives and a program switch never re-enables arrays. */
    glBindAttribLocation(program->id, ES2_ATTRIB_POSITION, "a_position");
    glBindAttribLocation(program->id, ES2_ATTRIB_TEXCOORD, "a_texcoord");
    glBindAttribLocation(program->id, ES2_ATTRIB_COLOR, "a_color");
    if( has_texinfo_attribute )
        glBindAttribLocation(program->id, ES2_ATTRIB_TEXINFO, "a_texinfo");
    if( has_mask_attribute )
        glBindAttribLocation(program->id, ES2_ATTRIB_MASK_TEXCOORD, "a_mask_texcoord");
    glLinkProgram(program->id);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    glGetProgramiv(program->id, GL_LINK_STATUS, &ok);
    if( !ok )
    {
        char log[1024];
        glGetProgramInfoLog(program->id, (GLsizei)sizeof(log), NULL, log);
        TORIRS_ERR("%s: %s: link failed: %s\n", g_es2_name, label, log);
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
    return es2_check_error(label);
}

static void
es2_delete_program(struct ES2Program* program)
{
    if( program->id )
        glDeleteProgram(program->id);
    memset(program, 0, sizeof(*program));
}

/* The interface layer's composite: the present's vertex shader, and the
 * two uniforms ES2Program has no field for. */
static bool
es2_link_ui_composite_program(struct TRSPK_Renderer_ES2* renderer)
{
    if( !es2_link_program(
            &renderer->program_ui_composite,
            es2_present_vertex_shader,
            es2_ui_composite_fragment_shader,
            false,
            false,
            "ui composite") )
        return false;
    renderer->ui_composite_u_size =
        glGetUniformLocation(renderer->program_ui_composite.id, "u_size");
    renderer->ui_composite_u_filter =
        glGetUniformLocation(renderer->program_ui_composite.id, "u_filter");
    return true;
}

static bool
es2_create_programs(struct TRSPK_Renderer_ES2* renderer)
{
    const char* shader_override=getenv("TORIRS_GLES2_FAST_SHADER");
    const char* gpu=(const char*)glGetString(GL_RENDERER);
    renderer->world_fast_shader=shader_override ? shader_override[0]!='0'
        : gpu && strstr(gpu,"Adreno") && strstr(gpu,"320");
    /* Fresh program objects hold no uniform values yet. */
    renderer->ui_projection_pushed = false;
    renderer->rotmask_projection_pushed = false;
    return es2_link_program(
               &renderer->program_world_plain,
               es2_world_vertex_shader,
               es2_world_plain_fragment_shader,
               false,
               true,
               "world (plain)") &&
        es2_link_program(
               &renderer->program_world_cutout,
               es2_world_vertex_shader,
               es2_world_cutout_fragment_shader,
               false,
               true,
               "world (cutout)") &&
        es2_link_program(&renderer->program_world_fast_plain,es2_world_vertex_shader,
               es2_world_fast_plain_fragment_shader,false,true,"world fast plain") &&
        es2_link_program(&renderer->program_world_fast_cutout,es2_world_vertex_shader,
               es2_world_fast_cutout_fragment_shader,false,true,"world fast cutout") &&
        es2_link_program(
               &renderer->program_ui,
               es2_ui_vertex_shader,
               es2_ui_fragment_shader,
               true,
               true,
               "ui") &&
        es2_link_program(
               &renderer->program_rotmask,
               es2_rotmask_vertex_shader,
               es2_rotmask_fragment_shader,
               true,
               false,
               "rotmask") &&
        es2_link_program(
               &renderer->program_present,
               es2_present_vertex_shader,
               es2_present_fragment_shader,
               false,
               false,
               "present") &&
        es2_link_ui_composite_program(renderer);
}

/* ---- letterbox and rectangles ---------------------------------------------- */

/* The output rect, the render target and the letterbox inside it.
 * `allow_offscreen` false draws direct even when the render size differs
 * (the boot bar, which has no frame end to present from). */
void
es2_update_letterbox(struct TRSPK_Renderer_ES2* renderer, bool allow_offscreen)
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
    renderer->target_offscreen = allow_offscreen &&
        (present.render_w != present.output.w || present.render_h != present.output.h);
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
es2_scissor_rect(
    const struct TRSPK_Renderer_ES2* renderer,
    int logical_x,
    int logical_y,
    int logical_width,
    int logical_height,
    struct ES2Rect* out)
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
    x0 = es2_clampi(logical_x, 0, renderer->width);
    y0 = es2_clampi(logical_y, 0, renderer->height);
    x1 = es2_clampi(logical_x + logical_width, 0, renderer->width);
    y1 = es2_clampi(logical_y + logical_height, 0, renderer->height);
    if( x1 <= x0 || y1 <= y0 )
        return false;
    left = renderer->letterbox_x +
        (int)((int64_t)x0 * renderer->letterbox_width / renderer->width);
    top = renderer->letterbox_top +
        (int)((int64_t)y0 * renderer->letterbox_height / renderer->height);
    right = renderer->letterbox_x +
        (int)(((int64_t)x1 * renderer->letterbox_width + renderer->width - 1) /
              renderer->width);
    bottom = renderer->letterbox_top +
        (int)(((int64_t)y1 * renderer->letterbox_height + renderer->height - 1) /
              renderer->height);
    left = es2_clampi(left, 0, renderer->target_width);
    right = es2_clampi(right, left, renderer->target_width);
    top = es2_clampi(top, 0, renderer->target_height);
    bottom = es2_clampi(bottom, top, renderer->target_height);
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
es2_decode_texture_rgba(
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
es2_reserve_upload_stage(struct TRSPK_Renderer_ES2* renderer, size_t needed)
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
es2_upload_atlas_texture(
    struct TRSPK_Renderer_ES2* renderer,
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
    es2_bind_texture0(renderer, texture);
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
    es2_reserve_upload_stage(renderer, (size_t)dirty.w * dirty.h * 4u);
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
es2_upload_atlas(struct TRSPK_Renderer_ES2* renderer)
{
    int64_t bytes = 0;
    assert(renderer);
    if( !renderer->gl_context )
        return false;
    if( !trspk_atlas_is_dirty(&renderer->atlas) && renderer->atlas_texture_allocated )
        return true;
    if( !es2_upload_atlas_texture(
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
es2_upload_ui_atlas_texture(struct TRSPK_Renderer_ES2* renderer, int64_t* out_bytes);
bool
es2_upload_ui_atlas_texture(struct TRSPK_Renderer_ES2* renderer, int64_t* out_bytes)
{
    assert(renderer);
    return es2_upload_atlas_texture(
        renderer,
        &renderer->ui_sprite_atlas,
        renderer->ui_sprite_atlas_texture,
        &renderer->ui_sprite_atlas_allocated,
        GL_NEAREST,
        out_bytes);
}

int
es2_texture_slot(struct TRSPK_Renderer_ES2* renderer, int tex_id)
{
    int slot;
    assert(renderer);
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return -1;
    slot = renderer->tex_slot_of_id[tex_id];
    if( slot >= 0 )
        return slot;
    if( renderer->tex_slot_next >= ES2_ATLAS_SLOTS )
    {
        static bool warned;
        if( !warned )
        {
            TORIRS_LOG("%s: the 2048x2048 world texture atlas is full\n", g_es2_name);
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
es2_texture_anim_bytes(
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
    speed = es2_clampi(texture->animation_speed, -127, 127);
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
es2_scene_texture(struct TRSPK_Renderer_ES2* renderer, int tex_id)
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
es2_refresh_anim_range(
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
es2_refresh_texture_animation(struct TRSPK_Renderer_ES2* renderer, int tex_id)
{
    const struct ToriDraw_Texture* texture = es2_scene_texture(renderer, tex_id);
    uint8_t anim_u;
    uint8_t anim_v;
    uint32_t group_index;
    uint32_t batch_slot;

    assert(renderer);
    es2_texture_anim_bytes(texture, &anim_u, &anim_v);
    for( group_index = 0u; group_index < TRSPK_VBO_GROUP_COUNT; group_index++ )
    {
        struct ES2ModelGroup* group = &renderer->groups[group_index];
        uint32_t slot_index;
        if( !group->arena || !group->vbo_cpu )
            continue;
        for( slot_index = 0u; slot_index < group->arena->slot_count; slot_index++ )
        {
            const struct TRSPK_ModelSlot* model_slot = &group->arena->slots[slot_index];
            if( !trspk_modelslot_is_alive(model_slot) )
                continue;
            (void)es2_refresh_anim_range(
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
        struct ES2StaticBatch* batch = &renderer->static_batches[batch_slot];
        uint32_t chunk_count;
        uint32_t chunk_index;
        if( !batch->cpu || (!batch->active && !batch->building) )
            continue;
        chunk_count = trspk_batch16_chunk_count(batch->cpu);
        for( chunk_index = 0u; chunk_index < chunk_count; chunk_index++ )
        {
            struct TRSPK_Batch16Chunk* chunk = trspk_batch16_get_chunk(batch->cpu, chunk_index);
            if( chunk && chunk->vbo &&
                es2_refresh_anim_range(
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
es2_load_texture_object(
    struct TRSPK_Renderer_ES2* renderer,
    int tex_id,
    const struct ToriDraw_Texture* texture)
{
    static uint8_t rgba[TRSPK_ATLAS_TILE * TRSPK_ATLAS_TILE * 4u];
    int slot;
    assert(renderer);
    assert(texture);
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY || !texture->texels )
        return false;
    slot = es2_texture_slot(renderer, tex_id);
    if( slot < 0 )
        return false;
    es2_decode_texture_rgba(texture, TRSPK_ATLAS_TILE, rgba);
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
        es2_refresh_texture_animation(renderer, tex_id);
    return true;
}

/** Reserve the slot and, when the scene already holds the texels, upload
 *  them. The slot is what a bake encodes, resident or not. */
int
es2_ensure_texture(struct TRSPK_Renderer_ES2* renderer, int tex_id)
{
    struct ToriDraw_Texture* texture;
    int slot;
    assert(renderer);
    if( tex_id < 0 )
        return -1;
    slot = es2_texture_slot(renderer, tex_id);
    if( slot < 0 )
        return -1;
    if( renderer->tex_resident[slot] )
        return slot;
    texture = es2_scene_texture(renderer, tex_id);
    if( texture )
        (void)es2_load_texture_object(renderer, tex_id, texture);
    return slot;
}

void
es2_unload_texture(struct TRSPK_Renderer_ES2* renderer, int tex_id)
{
    int slot;
    assert(renderer);
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return;
    slot = renderer->tex_slot_of_id[tex_id];
    if( slot >= 0 && (uint32_t)slot < ES2_ATLAS_SLOTS && renderer->atlas.pixels )
    {
        struct TRSPK_AtlasTile tile;
        /* A deferred widget-model draw samples the world atlas when it is
         * ISSUED, not when it was recorded; clearing this tile now would
         * reach the GPU on the next atlas upload, ahead of that draw. Issue
         * what is recorded first. */
        if( renderer->in2d )
            es2_ui_flush(renderer);
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
es2_map_atlas_uv(int slot, float local_u, float local_v, float* out_u, float* out_v)
{
    const float cell = (float)TRSPK_ATLAS_TILE / (float)ES2_ATLAS_DIM;
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
    *out_u = (float)(index & (ES2_ATLAS_COLS - 1u)) * cell + local_u * cell;
    *out_v = (float)(index / ES2_ATLAS_COLS) * cell + local_v * cell;
}

/* ---- per-frame stream sets --------------------------------------------------- */

/* Rotate every stream set onto this frame's buffer. */
void
es2_stream_sets_begin_frame(struct TRSPK_Renderer_ES2* renderer)
{
    struct ES2StreamSet* sets[4];
    uint32_t set_index;
    renderer->frame_slot = (renderer->frame_slot + 1u) % ES2_FRAMES_IN_FLIGHT;
    sets[0] = &renderer->index_stream;
    sets[1] = &renderer->dynamic_stream;
    sets[2] = &renderer->frame_stream;
    sets[3] = &renderer->ui_stream;
    for( set_index = 0u; set_index < 4u; set_index++ )
    {
        struct ES2StreamSet* set = sets[set_index];
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
 * buffer was last read ES2_FRAMES_IN_FLIGHT frames ago, so the write
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
uint32_t
es2_stream_set_append(
    struct ES2StreamSet* set,
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
es2_stream_set_destroy(struct ES2StreamSet* set)
{
    uint32_t slot;
    for( slot = 0u; slot < ES2_FRAMES_IN_FLIGHT; slot++ )
        if( set->buffers[slot] )
            glDeleteBuffers(1, &set->buffers[slot]);
    memset(set, 0, sizeof(*set));
}

/* ---- retained groups --------------------------------------------------------- */

static bool
es2_upload_group(struct TRSPK_Renderer_ES2* renderer, struct ES2ModelGroup* group)
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
        es2_bind_array_buffer(renderer, group->vbo_gpu);
    }

    if( group->reset_each_frame )
    {
        /* Rebuilt wholesale every frame, so it goes into this frame's buffer
         * of the dynamic stream set (see ES2_FRAMES_IN_FLIGHT). */
        uint32_t offset;
        byte_count = (size_t)vertex_count * sizeof(struct TRSPK_VertexGLES2);
        offset = es2_stream_set_append(
            &renderer->dynamic_stream,
            renderer->frame_slot,
            GL_ARRAY_BUFFER,
            ES2_DYNAMIC_STREAM_INIT_BYTES,
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
        uint32_t capacity = group->gpu_capacity ? group->gpu_capacity : ES2_GPU_BUFFER_INIT;
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
es2_reset_group(struct ES2ModelGroup* group)
{
    assert(group);
    if( group->arena )
        trspk_modelarena_clear(group->arena);
}

/* ---- static batches (Batch16 pages) ------------------------------------------ */

static bool
es2_upload_dirty_static_batches(struct TRSPK_Renderer_ES2* renderer);

static void
es2_mark_active_static_batches_dirty(struct TRSPK_Renderer_ES2* renderer)
{
    uint32_t batch_slot;
    assert(renderer);
    for( batch_slot = 0u; batch_slot < renderer->static_batch_count; batch_slot++ )
    {
        struct ES2StaticBatch* batch = &renderer->static_batches[batch_slot];
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
es2_grow_static_batches(struct TRSPK_Renderer_ES2* renderer, uint32_t needed)
{
    struct ES2StaticBatch* grown;
    uint32_t capacity;
    if( needed <= renderer->static_batch_capacity )
        return;
    capacity = renderer->static_batch_capacity ? renderer->static_batch_capacity : 8u;
    while( capacity < needed )
        capacity *= 2u;
    grown = (struct ES2StaticBatch*)realloc(
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
es2_static_batch_slot(struct TRSPK_Renderer_ES2* renderer, int batch_id, bool create)
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
    es2_grow_static_batches(renderer, renderer->static_batch_count + 1u);
    slot = renderer->static_batch_count++;
    renderer->static_batches[slot].batch_id = batch_id;
    renderer->static_batches[slot].cpu = trspk_batch16_create(TRSPK_VERTEX_FORMAT_GLES2);
    assert(renderer->static_batches[slot].cpu);
    return (int)slot;
}

void
es2_rebuild_batch_pose_table(struct TRSPK_Renderer_ES2* renderer)
{
    uint32_t batch_slot;
    assert(renderer);
    trspk_pose_table_clear(&renderer->batch_poses);
    for( batch_slot = 0u; batch_slot < renderer->static_batch_count; batch_slot++ )
    {
        const struct ES2StaticBatch* batch = &renderer->static_batches[batch_slot];
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
                entry_index > ES2_BATCH_POSE_ENTRY_MASK ||
                batch_slot > ES2_BATCH_POSE_SLOT_MASK )
                continue;
            page_id = batch->page_ids[entry->chunk_index];
            if( page_id >= renderer->static_page_count || !renderer->static_pages[page_id].valid )
                continue;
            trspk_pose_table_set(
                &renderer->batch_poses,
                entry->element_id,
                entry->anim_index,
                entry->pose_id,
                ES2_BATCH_POSE_FLAG | (batch_slot << ES2_BATCH_POSE_SLOT_SHIFT) | entry_index);
        }
    }
    es2_static_primary_rebuild(renderer);
}

static bool
es2_grow_static_pages(struct TRSPK_Renderer_ES2* renderer, uint32_t needed)
{
    struct ES2StaticPageRef* grown;
    uint32_t capacity;
    if( needed <= renderer->static_page_capacity )
        return true;
    if( needed > ES2_BATCH_PAGE_LIMIT )
        return false;
    capacity = renderer->static_page_capacity ? renderer->static_page_capacity : 32u;
    while( capacity < needed )
    {
        if( capacity >= ES2_BATCH_PAGE_LIMIT / 2u )
        {
            capacity = ES2_BATCH_PAGE_LIMIT;
            break;
        }
        capacity *= 2u;
    }
    grown = (struct ES2StaticPageRef*)realloc(
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
es2_static_batch_ensure_chunk_storage(struct ES2StaticBatch* batch, uint32_t chunk_count)
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
es2_static_batch_assign_page(
    struct TRSPK_Renderer_ES2* renderer,
    uint32_t batch_slot,
    uint32_t chunk_index)
{
    struct ES2StaticBatch* batch = &renderer->static_batches[batch_slot];
    const struct TRSPK_Batch16Chunk* chunk;
    struct ES2StaticPageRef* page;
    uint32_t page_id;
    uint32_t needed;
    assert(chunk_index < batch->page_id_capacity);
    chunk = trspk_batch16_get_chunk(batch->cpu, chunk_index);
    needed = chunk ? chunk->vertex_count : 0u;
    page_id = batch->page_ids[chunk_index];
    if( page_id == UINT32_MAX )
    {
        if( renderer->static_page_count >= ES2_BATCH_PAGE_LIMIT ||
            !es2_grow_static_pages(renderer, renderer->static_page_count + 1u) )
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
     * out (es2_compact_static_pages). */
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
es2_compact_static_pages(struct TRSPK_Renderer_ES2* renderer)
{
    uint32_t page_id;
    uint32_t used = 0u;
    assert(renderer);
    for( page_id = 0u; page_id < renderer->static_page_count; page_id++ )
    {
        struct ES2StaticPageRef* page = &renderer->static_pages[page_id];
        const struct ES2StaticBatch* batch;
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
    es2_mark_active_static_batches_dirty(renderer);
    return used;
}

void
es2_invalidate_batch_pages(struct TRSPK_Renderer_ES2* renderer, const struct ES2StaticBatch* batch)
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
es2_ensure_static_batch_vbo(
    struct TRSPK_Renderer_ES2* renderer,
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
        : ES2_STATIC_BATCH_VBO_INIT_VERTICES;
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
    es2_bind_array_buffer(renderer, renderer->static_batch_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)byte_capacity, NULL, GL_STATIC_DRAW);
    if( !es2_check_error("static batch page buffer") )
        return false;
    renderer->static_batch_gpu_vertex_capacity = capacity;
    *out_recreated = true;
    return true;
}

static bool
es2_upload_static_batch_chunk(
    struct TRSPK_Renderer_ES2* renderer,
    struct ES2StaticBatch* batch,
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
        es2_bind_array_buffer(renderer, renderer->static_batch_vbo);
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
es2_upload_dirty_static_batches(struct TRSPK_Renderer_ES2* renderer)
{
    uint32_t batch_slot;
    bool recreated = false;
    assert(renderer);
    if( !renderer->static_batch_upload_pending )
        return true;
    if( !es2_ensure_static_batch_vbo(
            renderer, renderer->static_batch_gpu_vertex_used, &recreated) )
        return false;
    if( recreated )
        es2_mark_active_static_batches_dirty(renderer);
    for( batch_slot = 0u; batch_slot < renderer->static_batch_count; batch_slot++ )
    {
        struct ES2StaticBatch* batch = &renderer->static_batches[batch_slot];
        uint32_t chunk_count;
        uint32_t chunk;
        if( !batch->active || !batch->cpu )
            continue;
        chunk_count = trspk_batch16_chunk_count(batch->cpu);
        for( chunk = 0u; chunk < chunk_count; chunk++ )
            if( !es2_upload_static_batch_chunk(renderer, batch, chunk) )
                return false;
    }
    renderer->static_batch_upload_pending = false;
    return true;
}

bool
es2_static_batch_commit(struct TRSPK_Renderer_ES2* renderer, uint32_t batch_slot)
{
    struct ES2StaticBatch* batch;
    uint32_t chunk_count;
    uint32_t chunk;
    bool recreated = false;

    assert(renderer);
    assert(batch_slot < renderer->static_batch_count);
    batch = &renderer->static_batches[batch_slot];
    assert(batch->cpu);
    batch->active = false;
    chunk_count = trspk_batch16_chunk_count(batch->cpu);
    es2_static_batch_ensure_chunk_storage(batch, chunk_count);
    es2_invalidate_batch_pages(renderer, batch);
    for( chunk = 0u; chunk < chunk_count; chunk++ )
        if( !es2_static_batch_assign_page(renderer, batch_slot, chunk) )
            goto fail;
    /* The tail ran past the buffer: pack the holes the rebuilt chunks left
     * before buying a bigger buffer, since either way everything re-sends. */
    if( renderer->static_batch_vbo &&
        renderer->static_batch_gpu_vertex_used > renderer->static_batch_gpu_vertex_capacity )
        (void)es2_compact_static_pages(renderer);
    if( !es2_ensure_static_batch_vbo(
            renderer, renderer->static_batch_gpu_vertex_used, &recreated) )
        goto fail;
    if( recreated || renderer->static_batch_upload_pending )
    {
        es2_mark_active_static_batches_dirty(renderer);
        if( !es2_upload_dirty_static_batches(renderer) )
            goto fail;
    }
    for( chunk = 0u; chunk < chunk_count; chunk++ )
        if( !es2_upload_static_batch_chunk(renderer, batch, chunk) )
            goto fail;
    batch->active = true;
    es2_rebuild_batch_pose_table(renderer);
    return true;

fail:
    es2_invalidate_batch_pages(renderer, batch);
    es2_rebuild_batch_pose_table(renderer);
    return false;
}

static bool
es2_resolve_static_page(
    struct TRSPK_Renderer_ES2* renderer,
    uint32_t page_id,
    struct TRSPK_Batch16Chunk** out_chunk)
{
    const struct ES2StaticPageRef* ref;
    struct ES2StaticBatch* batch;
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
es2_binding_cpu_source(
    struct TRSPK_Renderer_ES2* renderer,
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
    if( binding == ES2_STATIC_PAGE_BINDING )
    {
        struct TRSPK_Batch16Chunk* chunk = NULL;
        if( !es2_resolve_static_page(renderer, page_id, &chunk) )
            return false;
        *out_vbo = chunk->vbo;
        *out_triangles = &chunk->triangles;
        return *out_vbo != NULL;
    }
    if( binding == ES2_FRAME_STREAM_BINDING )
    {
        *out_vbo = renderer->frame_stream_cpu;
        *out_triangles = &renderer->frame_stream_triangles;
        return *out_vbo != NULL;
    }
    return false;
}

/* ---- pose tables and the static arena ------------------------------------------ */

static void
es2_rebuild_static_pose_table(struct TRSPK_Renderer_ES2* renderer)
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
es2_compact_static_group(struct TRSPK_Renderer_ES2* renderer)
{
    struct TRSPK_ModelArena* arena;
    struct TRSPK_ModelArenaGCResult result;
    assert(renderer);
    arena = renderer->groups[TRSPK_VBO_GROUP_STATIC].arena;
    if( !arena )
        return;
    result = trspk_modelarena_gc(arena);
    if( result.did_compact )
        es2_rebuild_static_pose_table(renderer);
}

static bool
es2_pose_element_is_retained(const struct TRSPK_Renderer_ES2* renderer, int element_id)
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
es2_pose_track_is_retained(
    const struct TRSPK_Renderer_ES2* renderer,
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
es2_bake_pose_vertices(
    struct TRSPK_Renderer_ES2* renderer,
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
    float* world_xyz=NULL;
    struct ToriDraw_Model* full_model=NULL;
    if( renderer->actor_world_cache_enabled && ordered_painter &&
        ToriDraw_ModelKindIsFull(model_handle.kind) )
    {
        full_model=(struct ToriDraw_Model*)ToriDraw_ModelRead(model_handle);
        if( written_count*3u>(uint32_t)full_model->vertex_count )
        {
            if( (uint32_t)full_model->vertex_count>renderer->actor_world_capacity )
            {
                float* grown=realloc(renderer->actor_world_xyz,(size_t)full_model->vertex_count*3*sizeof(float));
                if( grown ) {renderer->actor_world_xyz=grown;renderer->actor_world_capacity=(uint32_t)full_model->vertex_count;}
            }
            if( (uint32_t)full_model->vertex_count<=renderer->actor_world_capacity )
            {
                world_xyz=renderer->actor_world_xyz;
                trspk_toridraw_world_vertices(full_model,&placement,world_xyz);
            }
        }
    }
#if defined(TORIRS_BAKE_CHAIN_CAPTURE)
    es2_bake_capture_begin(renderer,model_handle,world_position,face_order,order_count);
#endif

#if !defined(TORIRS_BAKE_CHAIN_CAPTURE) && !defined(TORIRS_BAKE_VERIFY)
    /* This lane's fast encoder, or a decline. @see es2_bake_ordered_fast at
     * the top of this file: it is written out per lane, not selected. */
    if( es2_bake_ordered_fast(
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
        int config = ES2_TRIANGLE_UNTEXTURED;

        bool baked=false;
        if( face_index<(uint32_t)face_count )
        {
            if( world_xyz )
            {
                trspk_toridraw_bake_face_cached(full_model,face_index,&placement,NULL,
                    true,TRSPK_BAKE_COLOR_ARGB,world_xyz,&face);
                baked=true;
            }
            else
                baked=trspk_toridraw_bake_face_handle(model_handle,face_index,&placement,
                    NULL,true,TRSPK_BAKE_COLOR_ARGB,&face);
        }
        if( !baked )
        {
            /* A skipped face still owns its triplet in an ordered bake: leave
             * it fully transparent so the alpha test drops it. */
            if( face_order )
            {
                if( !ordered_painter ) trspk_triangles_set(
                    triangles, trspk_triangles_index_from_vertex(vertex), ES2_TRIANGLE_UNTEXTURED);
                trspk_vbo_write_vertex_gles2(vbo, vertex, 0.0f, 0.0f, 0.0f, 0u, 0.5f, 0.5f, 0u, 0u,
                    TRSPK_VERTEX_GLES2_ANIM_STILL, TRSPK_VERTEX_GLES2_ANIM_STILL);
                trspk_vbo_write_vertex_gles2(vbo, vertex + 1u, 0.0f, 0.0f, 0.0f, 0u, 0.5f, 0.5f, 0u,
                    0u, TRSPK_VERTEX_GLES2_ANIM_STILL, TRSPK_VERTEX_GLES2_ANIM_STILL);
                trspk_vbo_write_vertex_gles2(vbo, vertex + 2u, 0.0f, 0.0f, 0.0f, 0u, 0.5f, 0.5f, 0u,
                    0u, TRSPK_VERTEX_GLES2_ANIM_STILL, TRSPK_VERTEX_GLES2_ANIM_STILL);
            }
            continue;
        }

#if defined(TORIRS_BAKE_CHAIN_CAPTURE)
        es2_bake_capture_face(&face);
#endif
#if defined(TORIRS_BAKE_VERIFY)
        if( world_xyz )
        {
            struct TRSPK_ToriDrawBakeFaceVerts reference;
            trspk_toridraw_bake_face_handle(model_handle,face_index,&placement,renderer->scene,
                true,TRSPK_BAKE_COLOR_ARGB,&reference);
            struct BakeChainFace a=bake_chain_face(&face),b=bake_chain_face(&reference);
            if( memcmp(&a,&b,sizeof(a)) ){fprintf(stderr,"bake verification FAILED\n");abort();}
            static unsigned matched=0;
            if( (++matched%10000)==0 ) fprintf(stderr,"bake verification: %u real faces matched\n",matched);
        }
#endif
        if( face.tex_id >= 0 )
        {
            int slot = es2_ensure_texture(renderer, face.tex_id);
            config = face.tex_id;
            if( slot >= 0 )
            {
                tile_col = (uint8_t)((uint32_t)slot & (ES2_ATLAS_COLS - 1u));
                tile_row = (uint8_t)((uint32_t)slot / ES2_ATLAS_COLS);
                es2_texture_anim_bytes(
                    es2_scene_texture(renderer, face.tex_id), &anim_u, &anim_v);
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

        if( !ordered_painter ) trspk_triangles_set(triangles, trspk_triangles_index_from_vertex(vertex), config);
        trspk_vbo_write_vertex_gles2(
            vbo, vertex, face.wx_a, face.wy_a, face.wz_a,
            es2_argb_to_rgba_bytes(face.argb_a), ua, va, tile_col, tile_row, anim_u, anim_v);
        trspk_vbo_write_vertex_gles2(
            vbo, vertex + 1u, face.wx_b, face.wy_b, face.wz_b,
            es2_argb_to_rgba_bytes(face.argb_b), ub, vb, tile_col, tile_row, anim_u, anim_v);
        trspk_vbo_write_vertex_gles2(
            vbo, vertex + 2u, face.wx_c, face.wy_c, face.wz_c,
            es2_argb_to_rgba_bytes(face.argb_c), uc, vc, tile_col, tile_row, anim_u, anim_v);
    }
    /* Once for the model rather than three times per face -- and as a RANGE,
     * because this model is the only part of a shared retained buffer that
     * changed. */
#if defined(TORIRS_BAKE_CHAIN_CAPTURE)
    es2_bake_capture_end();
#endif
#if defined(TORIRS_BAKE_VERIFY)
    if( ordered_painter && world_xyz && !full_model->face_textures )
    {
        /* Verify the entire ordered direct stream, including placeholders,
         * against the generic final vertex writer above. The normal fast
         * return is suppressed in this diagnostic so both paths execute. */
        size_t bytes = (size_t)written_count * 3u * sizeof(struct TRSPK_VertexGLES2);
        struct TRSPK_VertexGLES2* direct = malloc(bytes ? bytes : 1u);
        if( !direct ) { fprintf(stderr, "direct bake verification allocation failed\n"); abort(); }
        /* This renderer declines the packed encoder, so the generic writer
         * above is the answer; kept so the diagnostic builds on every lane. */
        trspk_toridraw_gles2_untextured(full_model, face_order, written_count, world_xyz, direct);
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
            fprintf(stderr, "direct bake verification: %u models, %u packed faces matched\n",
                matched_models, matched_faces);
    }
#endif
    trspk_vbo_mark_dirty_range(vbo, vertex_base, written_count * 3u);
    return true;
}

uint32_t
es2_bake_into_arena(
    struct TRSPK_Renderer_ES2* renderer,
    struct ES2ModelGroup* group,
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
        TORIRS_ERR("%s: model has %lu vertices and cannot fit a 16-bit page\n",
            g_es2_name,
            (unsigned long)vertex_count);
        return UINT32_MAX;
    }
    anim_index = es2_clampi(anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1);
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
                es2_compact_static_group(renderer);
        }
    }
    slot_index = trspk_modelarena_load(group->arena, arena_element_id, arena_pose_id, vertex_count);
    model_slot = trspk_modelarena_get(group->arena, slot_index);
    if( !model_slot ||
        !es2_bake_pose_vertices(
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
es2_model_unload(struct TRSPK_Renderer_ES2* renderer, int element_id)
{
    assert(renderer);
    /* Individual unloads own only the arena. Batch geometry and its pose map
     * remain immutable until the matching batch rebuild/clear. */
    if( element_id < 0 || !renderer->groups[TRSPK_VBO_GROUP_STATIC].arena ||
        !es2_pose_element_is_retained(renderer, element_id) )
        return false;
    trspk_modelarena_unload_element(renderer->groups[TRSPK_VBO_GROUP_STATIC].arena, element_id);
    trspk_pose_table_remove_element(&renderer->poses, element_id);
    es2_compact_static_group(renderer);
    return true;
}

bool
es2_animation_track_unload(struct TRSPK_Renderer_ES2* renderer, int element_id, int anim_index)
{
    struct TRSPK_ModelArena* arena;
    uint32_t slot_index;
    assert(renderer);
    if( element_id < 0 || anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT )
        return false;
    arena = renderer->groups[TRSPK_VBO_GROUP_STATIC].arena;
    if( !arena || !es2_pose_track_is_retained(renderer, element_id, anim_index) )
        return false;
    for( slot_index = 0u; slot_index < arena->slot_count; slot_index++ )
    {
        const struct TRSPK_ModelSlot* slot = &arena->slots[slot_index];
        if( trspk_modelslot_is_alive(slot) && slot->element_id == element_id &&
            slot->pose_id % TRSPK_POSE_TRACK_COUNT == anim_index )
            trspk_modelarena_unload(arena, slot_index);
    }
    trspk_pose_table_remove_track(&renderer->poses, element_id, anim_index);
    es2_compact_static_group(renderer);
    return true;
}


/*
 * Whether ANIM_LOAD's command can be baked at all, and which pose track it
 * names. The track index is clamped here so every caller keys the arena,
 * the pose table and its own bookkeeping with the same number.
 */
bool
es2_animation_load_check(
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
    *out_anim_index = es2_clampi(command->anim_index, 0, TRSPK_POSE_TRACK_COUNT - 1);
    return true;
}

/*
 * One frame of the command's animation, posed into a throwaway model at
 * render scale. Never NULL; the caller bakes it and then ToriDraw_ModelFree's
 * it. Valid only for a command es2_animation_load_check accepted.
 */
struct ToriDraw_Model*
es2_animation_pose_frame(
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
    return baked;
}

bool
es2_reserve_model_indices(struct TRSPK_Renderer_ES2* renderer, uint32_t needed)
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
es2_bind_stream(struct TRSPK_Renderer_ES2* renderer, uint32_t binding, uint32_t page_base)
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
    else if( binding == ES2_STATIC_PAGE_BINDING )
        buffer = renderer->static_batch_vbo;
    else if( binding == ES2_FRAME_STREAM_BINDING )
    {
        buffer = renderer->frame_stream_vbo;
        base_vertex += renderer->frame_stream_gpu_base;
    }
    else if( binding == ES2_HOT_BINDING )
        buffer = renderer->hot_vbo;
    else
        return false;
    if( !buffer )
        return false;
    byte_offset = base_vertex * sizeof(struct TRSPK_VertexGLES2);
    assert(byte_offset <= (uint64_t)INT32_MAX);
    if( renderer->stream_layout == ES2_STREAM_WORLD && renderer->stream_buffer == buffer &&
        renderer->stream_byte_offset == (uint32_t)byte_offset )
        return true;
    es2_bind_array_buffer(renderer, buffer);
    glVertexAttribPointer(
        ES2_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, ES2_VERTEX_STRIDE,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct TRSPK_VertexGLES2, position)));
    glVertexAttribPointer(
        ES2_ATTRIB_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, ES2_VERTEX_STRIDE,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct TRSPK_VertexGLES2, rgba)));
    glVertexAttribPointer(
        ES2_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, ES2_VERTEX_STRIDE,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct TRSPK_VertexGLES2, texcoord)));
    glVertexAttribPointer(
        ES2_ATTRIB_TEXINFO, 4, GL_UNSIGNED_BYTE, GL_FALSE, ES2_VERTEX_STRIDE,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct TRSPK_VertexGLES2, tile_col)));
    if( renderer->stream_layout == ES2_STREAM_ROTMASK )
        glEnableVertexAttribArray(ES2_ATTRIB_TEXINFO);
    renderer->stream_buffer = buffer;
    renderer->stream_byte_offset = (uint32_t)byte_offset;
    renderer->stream_layout = ES2_STREAM_WORLD;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATTRIB_REBINDS, 1);
    return true;
}

void
es2_bind_ui_stream(struct TRSPK_Renderer_ES2* renderer, uint32_t byte_offset)
{
    const GLsizei stride = (GLsizei)sizeof(struct ES2VertexUI);
    assert(renderer);
    if( renderer->stream_layout == ES2_STREAM_UI && renderer->stream_buffer == renderer->ui_vbo &&
        renderer->stream_byte_offset == byte_offset )
        return;
    es2_bind_array_buffer(renderer, renderer->ui_vbo);
    glVertexAttribPointer(
        ES2_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES2VertexUI, x)));
    glVertexAttribPointer(
        ES2_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES2VertexUI, u)));
    glVertexAttribPointer(
        ES2_ATTRIB_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES2VertexUI, rgba)));
    glVertexAttribPointer(
        ES2_ATTRIB_TEXINFO, 1, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES2VertexUI, sel)));
    if( renderer->stream_layout == ES2_STREAM_ROTMASK )
        glEnableVertexAttribArray(ES2_ATTRIB_TEXINFO);
    renderer->stream_buffer = renderer->ui_vbo;
    renderer->stream_byte_offset = byte_offset;
    renderer->stream_layout = ES2_STREAM_UI;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATTRIB_REBINDS, 1);
}

void
es2_bind_rotmask_stream(struct TRSPK_Renderer_ES2* renderer, uint32_t byte_offset)
{
    const GLsizei stride = (GLsizei)sizeof(struct ES2VertexRotmask);
    assert(renderer);
    /* Attribute pointers are context state that outlives the draw (ES 2.0
     * has no VAO; §2.8 vertex array state persists until re-pointed), so a
     * second rotmask draw from the same offset needs no re-issue. */
    if( renderer->stream_layout == ES2_STREAM_ROTMASK && renderer->stream_buffer == renderer->ui_vbo &&
        renderer->stream_byte_offset == byte_offset )
        return;
    es2_bind_array_buffer(renderer, renderer->ui_vbo);
    glVertexAttribPointer(
        ES2_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES2VertexRotmask, x)));
    glVertexAttribPointer(
        ES2_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES2VertexRotmask, u)));
    glVertexAttribPointer(
        ES2_ATTRIB_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES2VertexRotmask, rgba)));
    glVertexAttribPointer(
        ES2_ATTRIB_MASK_TEXCOORD, 2, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)(byte_offset + offsetof(struct ES2VertexRotmask, mask_u)));
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
    glEnableVertexAttribArray(ES2_ATTRIB_MASK_TEXCOORD);
    renderer->stream_buffer = renderer->ui_vbo;
    renderer->stream_byte_offset = byte_offset;
    renderer->stream_layout = ES2_STREAM_ROTMASK;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATTRIB_REBINDS, 1);
}

/*
 * The 2D ring. Every flush appends at the head instead of rewriting offset
 * 0, so the driver never has to synchronise a write against the draw that is
 * still reading. Wrapping orphans the whole buffer with glBufferData(NULL),
 * which is the ES2 idiom for "give me fresh storage, keep the old for the GPU".
 * `earlier_appends_drawn`: see es2_stream_set_append.
 */
uint32_t
es2_ring_upload(
    struct TRSPK_Renderer_ES2* renderer,
    const void* data,
    uint32_t bytes,
    bool earlier_appends_drawn)
{
    uint32_t offset;
    assert(renderer);
    assert(data);
    offset = es2_stream_set_append(
        &renderer->ui_stream,
        renderer->frame_slot,
        GL_ARRAY_BUFFER,
        ES2_UI_STREAM_INIT_BYTES,
        data,
        bytes,
        earlier_appends_drawn);
    renderer->bound_array_buffer = renderer->ui_vbo;
    return offset;
}

/* ---- the world draw ------------------------------------------------------------- */

void
es2_use_world_program(struct TRSPK_Renderer_ES2* renderer, bool cutout)
{
    const struct ES2Program* program;
    assert(renderer);
    program = renderer->world_fast_shader
        ? (cutout ? &renderer->program_world_fast_cutout : &renderer->program_world_fast_plain)
        : (cutout ? &renderer->program_world_cutout : &renderer->program_world_plain);
    es2_use_program(renderer, program);
    glUniformMatrix4fv(program->u_matrix, 1, GL_FALSE, renderer->model_view_projection);
    /* Reduced modulo 128 on the CPU: speed / 128 texels per tick means the
     * scroll repeats every 128 ticks, and a float clock that never grows past
     * 128 keeps the fract() in the shader exact. */
    glUniform1f(program->u_clock, (float)fmod(renderer->frame_clock, 128.0));
    es2_bind_texture0(renderer, renderer->atlas_texture);
}

bool
es2_upload_geometry(struct TRSPK_Renderer_ES2* renderer)
{
    uint32_t group;
    assert(renderer);
    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
        if( !es2_upload_group(renderer, &renderer->groups[group]) )
            return false;
    return es2_upload_dirty_static_batches(renderer);
}

/* ---- the draw sequence and the two per-frame rings -------------------------------- */

void
es2_sequence_reset(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
    renderer->draw_item_count = 0u;
    renderer->ibo_staging_count = 0u;
    renderer->frame_stream_count = 0u;
    renderer->hot_frame_oldest_serial = UINT64_MAX;
}

static struct ES2DrawItem*
es2_sequence_append(struct TRSPK_Renderer_ES2* renderer)
{
    if( renderer->draw_item_count >= renderer->draw_item_capacity )
    {
        uint32_t capacity = renderer->draw_item_capacity ? renderer->draw_item_capacity * 2u
                                                         : ES2_DRAW_ITEM_INIT;
        struct ES2DrawItem* grown = (struct ES2DrawItem*)realloc(
            renderer->draw_items, (size_t)capacity * sizeof(*grown));
        assert(grown);
        renderer->draw_items = grown;
        renderer->draw_item_capacity = capacity;
    }
    return &renderer->draw_items[renderer->draw_item_count++];
}

void
es2_sequence_push_indexed(
    struct TRSPK_Renderer_ES2* renderer,
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
    destination = es2_sequence_reserve_indexed(renderer, index_count);
    memcpy(destination, indices, (size_t)index_count * sizeof(*indices));
    es2_sequence_commit_indexed(renderer, binding, page_base, cutout, blended, index_count);
}

uint16_t*
es2_sequence_reserve_indexed(
    struct TRSPK_Renderer_ES2* renderer,
    uint32_t index_count)
{
    uint32_t needed;
    assert(renderer);
    needed = renderer->ibo_staging_count + index_count;
    if( needed > renderer->ibo_staging_capacity )
    {
        uint32_t capacity = renderer->ibo_staging_capacity ? renderer->ibo_staging_capacity
                                                           : ES2_GPU_BUFFER_INIT;
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
es2_sequence_commit_indexed(
    struct TRSPK_Renderer_ES2* renderer,
    uint32_t binding,
    uint32_t page_base,
    bool cutout,
    bool blended,
    uint32_t index_count)
{
    struct ES2DrawItem* item;
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
        item = es2_sequence_append(renderer);
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
es2_sequence_push_array(
    struct TRSPK_Renderer_ES2* renderer,
    uint32_t binding,
    uint32_t first,
    uint32_t count,
    bool cutout,
    bool blended)
{
    struct ES2DrawItem* item;
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
    item = es2_sequence_append(renderer);
    item->binding = binding;
    item->page_base = 0u;
    item->first = first;
    item->count = count;
    item->indexed = 0u;
    item->cutout = (uint8_t)cutout;
    item->blended = (uint8_t)blended;
}

uint32_t
es2_frame_stream_reserve(struct TRSPK_Renderer_ES2* renderer, uint32_t vertex_count)
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
es2_frame_stream_upload(struct TRSPK_Renderer_ES2* renderer)
{
    uint32_t bytes;
    uint32_t offset;
    if( renderer->frame_stream_count == 0u )
        return;
    bytes = renderer->frame_stream_count * (uint32_t)sizeof(struct TRSPK_VertexGLES2);
    offset = es2_stream_set_append(
        &renderer->frame_stream,
        renderer->frame_slot,
        GL_ARRAY_BUFFER,
        ES2_FRAME_STREAM_INIT_BYTES,
        renderer->frame_stream_cpu->vertices.as_gles2,
        bytes,
        false);
    renderer->bound_array_buffer = renderer->frame_stream_vbo;
    renderer->frame_stream_gpu_base = offset / (uint32_t)sizeof(struct TRSPK_VertexGLES2);
    /* The stream just moved; the attribute pointers must follow it. */
    if( renderer->stream_buffer == renderer->frame_stream_vbo )
        renderer->stream_layout = ES2_STREAM_NONE;
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DYNAMIC_VBO_UPLOAD_BYTES, (int64_t)bytes);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DYNAMIC_VBO_UPLOADS, 1);
}


#if defined(TORIRS_SHADER_PROBE)
#include "../../tools/perf/es2_shader_probe.u.h"
#endif


/* ---- the 3D pass ------------------------------------------------------------------- */

void
es2_mat4_multiply(const float* a, const float* b, float* out)
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
es2_set_letterbox_viewport(struct TRSPK_Renderer_ES2* renderer)
{
    glViewport(
        renderer->letterbox_x,
        renderer->letterbox_y,
        renderer->letterbox_width,
        renderer->letterbox_height);
}


/* ---- batch commands --------------------------------------------------------------- */



void
es2_batch_end(struct TRSPK_Renderer_ES2* renderer, const struct ToriRS_RenderCommand_Batch* command)
{
    struct ES2StaticBatch* batch;
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
    (void)es2_static_batch_commit(renderer, (uint32_t)slot);
    renderer->current_batch_slot = -1;
}


/* ---- dispatch ---------------------------------------------------------------------- */


/* ---- lifetime ----------------------------------------------------------------------- */

/*
 * The rotmask source generation. The sprites a rotmask slot draws from (the
 * minimap bake, UITREE_SCENE_WORLD_MAP_SPRITE_ID) are rewritten IN PLACE by
 * app_rebuild_world_map with no event the renderer sees; the renderer used to
 * discover a rewrite by hashing the whole 512x512 bake every eighth frame.
 * The producer knows when it rewrote, so it says so: a bump here, and every
 * rotmask slot re-uploads on its next draw. Process-wide rather than per
 * renderer because the caller (app.c) holds no renderer.
 */
static uint32_t g_es2_rotmask_source_generation = 1u;

void
TRSPK_Renderer_ES2_RotmaskSourceChanged(void)
{
    g_es2_rotmask_source_generation++;
    if( g_es2_rotmask_source_generation == 0u )
        g_es2_rotmask_source_generation = 1u; /* 0 is "never uploaded" in a slot */
}

uint32_t
es2_rotmask_source_generation(void)
{
    return g_es2_rotmask_source_generation;
}

struct TRSPK_Renderer_ES2*
TRSPK_Renderer_ES2_New(int width, int height, char const* name)
{
    struct TRSPK_Renderer_ES2* renderer;
    static uint8_t white_tile[TRSPK_ATLAS_TILE * TRSPK_ATLAS_TILE * 4u];
    uint32_t group;
    int texture;

    assert(width > 0);
    assert(height > 0);
    assert(name);
    renderer = (struct TRSPK_Renderer_ES2*)calloc(1u, sizeof(*renderer));
    assert(renderer);
    renderer->name = name;
    g_es2_name = name;
    renderer->width = width;
    renderer->height = height;
    renderer->interface_scale_mode = 2;
    renderer->tex_slot_next = 1u;
    renderer->current_batch_slot = -1;
    /* TORIRS_GLES2_DEBUG=1: the 300-frame counters and the debug-only GL
     * error checks. Read once here; it used to be a getenv in es2_end_3d. */
    renderer->debug = getenv("TORIRS_GLES2_DEBUG") != NULL;
    /* The levers (see the struct): each defaults ON; NAME=0 is the control
     * arm. Read once, here, so no frame ever scans the environment. */
    {
        const char* v=getenv("TORIRS_GLES2_ACTOR_WORLD_CACHE");
#if defined(__arm__) && (defined(__ARM_NEON) || defined(__ARM_NEON__))
        renderer->actor_world_cache_enabled=!v || v[0]!='0';
#else
        renderer->actor_world_cache_enabled=v && v[0]=='1';
#endif
    }
    { const char* v=getenv("TORIRS_GLES2_FAST_SHADER");renderer->world_fast_shader=v && v[0]=='1'; }
    for( texture = 0; texture < TORIDRAW_TEXTURE_ID_CAPACITY; texture++ )
        renderer->tex_slot_of_id[texture] = -1;
    trspk_pose_table_init(&renderer->poses);
    trspk_pose_table_init(&renderer->batch_poses);
    renderer->frame_stream_cpu = trspk_vbo_create(0u, TRSPK_VERTEX_FORMAT_GLES2);
    assert(renderer->frame_stream_cpu);
    if( !trspk_atlas_init_grid(
            &renderer->atlas,
            ES2_ATLAS_DIM,
            ES2_ATLAS_DIM,
            TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE,
            4u) )
    {
        TRSPK_Renderer_ES2_Free(renderer);
        return NULL;
    }
    memset(white_tile, 0xff, sizeof(white_tile));
    if( !trspk_atlas_grid_insert_at(
            &renderer->atlas, 0u, white_tile, TRSPK_ATLAS_TILE * 4u, TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE, NULL) )
    {
        TRSPK_Renderer_ES2_Free(renderer);
        return NULL;
    }
    renderer->tex_resident[0] = 1u;
    for( group = 0u; group < TRSPK_VBO_GROUP_COUNT; group++ )
    {
        struct ES2ModelGroup* model_group = &renderer->groups[group];
        model_group->vbo_cpu = trspk_vbo_create(0u, TRSPK_VERTEX_FORMAT_GLES2);
        assert(model_group->vbo_cpu);
        model_group->arena = trspk_modelarena_create(
            model_group->vbo_cpu, &model_group->triangles, ES2_VBO_PAGE, 64u);
        assert(model_group->arena);
        model_group->reset_each_frame = group == TRSPK_VBO_GROUP_DYNAMIC;
    }
    es2_ui_init_state(renderer);
    return renderer;
}

static uint64_t
es2_pose_table_bytes(const struct TRSPK_PoseTable* table)
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
es2_report_retained_memory(struct TRSPK_Renderer_ES2* renderer)
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
        const struct ES2ModelGroup* model_group = &renderer->groups[group];
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
    pose_table_megabytes = ((double)es2_pose_table_bytes(&renderer->poses) +
                               (double)es2_pose_table_bytes(&renderer->batch_poses)) /
        1048576.0;
    /* TORIRS_LOG compiles out of a release build; the figure is still computed
     * so the function that produces it is not dead code there. */
    (void)pose_table_megabytes;
    TORIRS_LOG("es2_mem: === retained memory report ===\n"
               "es2_mem: batch16_cpu_vertices  %10.2f MB (%u chunks)\n"
               "es2_mem: batch16_cpu_configs   %10.2f MB\n"
               "es2_mem: static_pages_gpu      %10.2f MB (%u pages)\n"
               "es2_mem: group_static_cpu      %10.2f MB (vbo) + %.2f MB (configs) + %.2f MB (slots)\n"
               "es2_mem: group_static_gpu      %10.2f MB\n"
               "es2_mem: group_dynamic_cpu     %10.2f MB (vbo) + %.2f MB (configs)\n"
               "es2_mem: group_dynamic_gpu     %10.2f MB\n"
               "es2_mem: index_stream_gpu      %10.2f MB (one of %u)\n"
               "es2_mem: frame_stream_gpu      %10.2f MB (one of %u) + %.2f MB (cpu)\n"
               "es2_mem: draw_items_cpu        %10.2f MB\n"
               "es2_mem: ibo_staging_cpu       %10.2f MB\n"
               "es2_mem: model_indices_cpu     %10.2f MB\n"
               "es2_mem: atlas_cpu             %10.2f MB world + %.2f MB ui\n"
               "es2_mem: pose_tables_cpu       %10.2f MB\n",
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
        ES2_FRAMES_IN_FLIGHT,
        (double)renderer->frame_stream.capacities[renderer->frame_slot] / 1048576.0,
        ES2_FRAMES_IN_FLIGHT,
        renderer->frame_stream_cpu
            ? (double)renderer->frame_stream_cpu->capacity * sizeof(struct TRSPK_VertexGLES2) /
                1048576.0
            : 0.0,
        (double)renderer->draw_item_capacity * sizeof(struct ES2DrawItem) / 1048576.0,
        (double)renderer->ibo_staging_capacity * sizeof(uint16_t) / 1048576.0,
        (double)renderer->model_index_capacity * sizeof(uint16_t) / 1048576.0,
        (double)renderer->atlas.stride * renderer->atlas.height / 1048576.0,
        (double)renderer->ui_sprite_atlas.stride * renderer->ui_sprite_atlas.height / 1048576.0,
        pose_table_megabytes);
    es2_ui_report_memory(renderer);
    es2_zbuffer_report_memory(renderer);
}

/* ---- client scaling's offscreen target --------------------------------------- */

void
es2_scale_target_destroy_buffers(struct TRSPK_Renderer_ES2* renderer)
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

void
es2_bind_present_quad(struct TRSPK_Renderer_ES2* renderer)
{
    const GLsizei stride = (GLsizei)sizeof(struct ES2VertexUI);

    assert(renderer);
    if( !renderer->present_vbo )
    {
        /* Two triangles over clip space; v = 0 is the texture's bottom row,
         * which is GL's bottom row of the frame too. */
        static const float corners[6][4] = {
            { -1.0f, -1.0f, 0.0f, 0.0f }, { 1.0f, -1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f },
            { -1.0f, -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f },  { -1.0f, 1.0f, 0.0f, 1.0f },
        };
        struct ES2VertexUI vertices[6];
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
        glGenBuffers(1, &renderer->present_vbo);
        assert(renderer->present_vbo);
        es2_bind_array_buffer(renderer, renderer->present_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(vertices), vertices, GL_STATIC_DRAW);
    }
    es2_bind_array_buffer(renderer, renderer->present_vbo);
    /* Every enabled array points at valid data, used by the program or not:
     * a stray enabled array drops the draw on some drivers. */
    glVertexAttribPointer(
        ES2_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)offsetof(struct ES2VertexUI, x));
    glVertexAttribPointer(
        ES2_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)offsetof(struct ES2VertexUI, u));
    glVertexAttribPointer(
        ES2_ATTRIB_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
        (const void*)(uintptr_t)offsetof(struct ES2VertexUI, rgba));
    glVertexAttribPointer(
        ES2_ATTRIB_TEXINFO, 1, GL_FLOAT, GL_FALSE, stride,
        (const void*)(uintptr_t)offsetof(struct ES2VertexUI, sel));
    renderer->stream_buffer = renderer->present_vbo;
    renderer->stream_byte_offset = 0u;
    renderer->stream_layout = ES2_STREAM_NONE;
}

static void
es2_scale_target_destroy(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
    es2_scale_target_destroy_buffers(renderer);
    if( renderer->present_vbo )
    {
        if( renderer->bound_array_buffer == renderer->present_vbo )
            renderer->bound_array_buffer = 0u;
        if( renderer->stream_buffer == renderer->present_vbo )
            renderer->stream_layout = ES2_STREAM_NONE;
        glDeleteBuffers(1, &renderer->present_vbo);
    }
    renderer->present_vbo = 0u;
}

/* The offscreen target at this frame's render size, (re)made only when the
 * size changes. Colour is an RGBA texture with no mipmaps and clamped edges,
 * which WebGL1 accepts at any size; depth only on the depth-buffered lane. */
void
es2_scale_target_ensure(struct TRSPK_Renderer_ES2* renderer)
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
            es2_bind_texture0(renderer, renderer->scale_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            renderer->scale_texture_filter = filter;
            /* Never left bound while the frame draws into it. */
            es2_bind_texture0(renderer, 0u);
        }
        return;
    }

    es2_scale_target_destroy_buffers(renderer);
    glGenTextures(1, &renderer->scale_texture);
    assert(renderer->scale_texture);
    es2_bind_texture0(renderer, renderer->scale_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        renderer->target_width,
        renderer->target_height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        NULL);
    es2_bind_texture0(renderer, 0u);
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
            g_es2_name,
            renderer->target_width,
            renderer->target_height,
            (unsigned)status);
    assert(status == GL_FRAMEBUFFER_COMPLETE);
    renderer->scale_fbo_width = renderer->target_width;
    renderer->scale_fbo_height = renderer->target_height;
}

/*
 * The depth renderbuffer for the offscreen target, attached to whatever
 * es2_scale_target_ensure last built. Idempotent: the colour rebuild is what
 * drops it, so a live ::scale_depth is already the right size.
 *
 * GL_DEPTH_COMPONENT16 is the only renderbuffer depth format ES 2.0
 * guarantees, so this offscreen pass is a little shallower than the direct
 * one, which asks the window for 16 and may be handed more.
 */
void
es2_scale_target_ensure_depth(struct TRSPK_Renderer_ES2* renderer)
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
    glRenderbufferStorage(
        GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, renderer->target_width, renderer->target_height);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderer->scale_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if( status != GL_FRAMEBUFFER_COMPLETE )
        TORIRS_ERR(
            "%s: offscreen depth %dx%d incomplete: 0x%x\n",
            g_es2_name,
            renderer->target_width,
            renderer->target_height,
            (unsigned)status);
    assert(status == GL_FRAMEBUFFER_COMPLETE);
}

/* The finished offscreen frame onto the output rect of the drawable, with
 * the bars cleared black. GLES2 has no blit: one textured quad. */
void
es2_scale_target_present(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
    assert(renderer->scale_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    es2_set_scissor(renderer, NULL);
    es2_set_blend(renderer, false);
    es2_set_cull(renderer, false);
    /* Depth write on so the clear reaches the depth buffer. */
    es2_set_depth(renderer, false, true);
    glViewport(0, 0, renderer->drawable_width, renderer->drawable_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(
        renderer->output_x, renderer->output_y, renderer->output_width, renderer->output_height);

    es2_use_program(renderer, &renderer->program_present);
    es2_bind_texture0(renderer, renderer->scale_texture);
    es2_bind_present_quad(renderer);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void
es2_destroy_gl_resources(struct TRSPK_Renderer_ES2* renderer)
{
    uint32_t group;
    assert(renderer);
    es2_ui_destroy_gl(renderer);
    es2_delete_program(&renderer->program_world_plain);
    es2_delete_program(&renderer->program_world_cutout);
    es2_delete_program(&renderer->program_world_fast_plain);
    es2_delete_program(&renderer->program_world_fast_cutout);
    es2_delete_program(&renderer->program_ui);
    es2_delete_program(&renderer->program_rotmask);
    es2_delete_program(&renderer->program_present);
    es2_delete_program(&renderer->program_ui_composite);
    es2_scale_target_destroy(renderer);
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
    es2_stream_set_destroy(&renderer->index_stream);
    es2_stream_set_destroy(&renderer->dynamic_stream);
    es2_stream_set_destroy(&renderer->frame_stream);
    es2_stream_set_destroy(&renderer->ui_stream);
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
TRSPK_Renderer_ES2_Free(struct TRSPK_Renderer_ES2* renderer)
{
    uint32_t batch;
    uint32_t group;
    if( !renderer )
        return;
    es2_report_retained_memory(renderer);
    if( renderer->gl_context )
    {
        ToriPlatform_GLContext_MakeCurrent(renderer->window, renderer->gl_context);
        es2_destroy_gl_resources(renderer);
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
    free(renderer->actor_world_xyz);
    if( renderer->frame_stream_cpu )
        trspk_vbo_free(renderer->frame_stream_cpu);
    trspk_triangles_free(&renderer->frame_stream_triangles);
    free(renderer->draw_items);
    trspk_pose_table_free(&renderer->poses);
    trspk_pose_table_free(&renderer->batch_poses);
    free(renderer->static_primary);
    free(renderer->static_primary_bits);
    es2_zbuffer_destroy(renderer);
    for( batch = 0u; batch < renderer->static_batch_count; batch++ )
    {
        trspk_batch16_destroy(renderer->static_batches[batch].cpu);
        free(renderer->static_batches[batch].page_ids);
        free(renderer->static_batches[batch].hot_serial);
    }
    if( trspk_atlas_is_initialized(&renderer->atlas) )
        trspk_atlas_free(&renderer->atlas);
    es2_ui_free(renderer);
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
 * calls itself "depth-buffered". @see TRSPK_Renderer_ES2_PainterInit,
 * TRSPK_Renderer_ES2_ZBufferInit.
 */
bool
es2_init_gl(
    struct TRSPK_Renderer_ES2* renderer,
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
     * it is a parameter of the create call. 16 bits: the format every GLES2
     * device offers; EGL treats the request as a floor, so a device with more
     * may hand more back. */
    renderer->gl_context = ToriPlatform_GLContext_Create(window, depth_bits, TORIPLATFORM_GL_CLIENT_ES2);
    if( !renderer->gl_context )
    {
        TORIRS_ERR("%s: context creation failed: %s\n", g_es2_name, ToriPlatform_GLContext_LastError());
        return false;
    }
    ToriPlatform_GLContext_SetSwapInterval(0);

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    TORIRS_LOG("%s: %s | GLSL %s | %s | max texture %d\n",
        g_es2_name,
        (const char*)glGetString(GL_VERSION),
        (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION),
        (const char*)glGetString(GL_RENDERER),
        (int)max_texture_size);
    if( max_texture_size < (GLint)ES2_ATLAS_DIM )
    {
        TORIRS_ERR("%s: GL_MAX_TEXTURE_SIZE %d is below the %u atlas this renderer needs\n",
            g_es2_name,
            (int)max_texture_size,
            ES2_ATLAS_DIM);
        goto fail;
    }
    if( !es2_create_programs(renderer) )
        goto fail;

    glGenTextures(1, &renderer->atlas_texture);
    if( !es2_upload_atlas(renderer) )
        goto fail;
    if( !es2_ui_create_gl(renderer) )
        goto fail;

    /* The first four attributes are live for the life of the context (the
     * fourth is the world's texinfo and the UI's sampler select; the rotmask
     * layout keeps it pointed at something valid); the mask uv follows the
     * rotmask layout. */
    glEnableVertexAttribArray(ES2_ATTRIB_POSITION);
    glEnableVertexAttribArray(ES2_ATTRIB_TEXCOORD);
    glEnableVertexAttribArray(ES2_ATTRIB_COLOR);
    glEnableVertexAttribArray(ES2_ATTRIB_TEXINFO);
    es2_state_reset(renderer);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    if( !es2_check_error("init") )
        goto fail;

    if( renderer->static_page_count > 0u )
    {
        bool recreated = false;
        if( !es2_ensure_static_batch_vbo(renderer, renderer->static_page_count, &recreated) )
            goto fail;
        if( recreated )
            es2_mark_active_static_batches_dirty(renderer);
        if( !es2_upload_dirty_static_batches(renderer) )
            goto fail;
    }
    TORIRS_LOG("%s: renderer up (%s world pass)\n", g_es2_name, world_pass);
    return true;

fail:
    es2_destroy_gl_resources(renderer);
    ToriPlatform_GLContext_Delete(renderer->gl_context);
    renderer->gl_context = NULL;
    return false;
}

void
TRSPK_Renderer_ES2_SetViewport(struct TRSPK_Renderer_ES2* renderer, int width, int height)
{
    assert(renderer);
    if( width <= 0 || height <= 0 || (renderer->width == width && renderer->height == height) )
        return;
    renderer->width = width;
    renderer->height = height;
    es2_update_letterbox(renderer, renderer->target_offscreen);
    renderer->in2d = false;
    es2_ui_batch_reset(renderer);
}

void
TRSPK_Renderer_ES2_SetInterfaceScaleMode(struct TRSPK_Renderer_ES2* renderer, int mode)
{
    assert(renderer);
    /* Read at the next BEGIN_2D (es2_ui_layer_wanted); interface art is
     * always sampled nearest, so no texture is refiltered. */
    renderer->interface_scale_mode = es2_clampi(mode, 0, 2);
}

void
TRSPK_Renderer_ES2_SetClientScaling(
    struct TRSPK_Renderer_ES2* renderer,
    struct ClientScaleSettings const* settings)
{
    assert(renderer);
    assert(settings);
    renderer->client_scale = *settings;
}

void
TRSPK_Renderer_ES2_SetPick(struct TRSPK_Renderer_ES2* renderer, int mouse_x, int mouse_y)
{
    assert(renderer);
    renderer->pick_enabled = true;
    renderer->pick_mouse_x = mouse_x;
    renderer->pick_mouse_y = mouse_y;
    ToriRS_PickHitsReset(&renderer->pick_hits);
}

struct ToriRS_PickHits const*
TRSPK_Renderer_ES2_PickHits(struct TRSPK_Renderer_ES2 const* renderer)
{
    assert(renderer);
    return &renderer->pick_hits;
}

/*
 * TORIRS_GLES2_READBACK=path dumps one finished frame, through the same
 * readback the app's screenshots use, so a bug in the letterbox arithmetic
 * cannot show in a debug dump and not in a screenshot. Both composing
 * renderers end their frame with it.
 */
void
es2_frame_readback(struct TRSPK_Renderer_ES2* renderer)
{
    assert(renderer);
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
        if( TRSPK_Renderer_ES2_ReadPixels(renderer, top, renderer->width, renderer->height) )
        {
            bmp_write_file(path, top, renderer->width, renderer->height);
            TORIRS_LOG("es2_readback: wrote %s\n", path);
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
 * composing renderer's to say -- @see es2p_begin_frame and es2z_begin_frame,
 * which bracket this with their own two lines and then call
 * es2_frame_surface_clear.
 */
bool
es2_frame_surface_begin(struct TRSPK_Renderer_ES2* renderer, bool allow_offscreen)
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
    es2_update_letterbox(renderer, allow_offscreen);
    es2_state_reset(renderer);
    es2_stream_sets_begin_frame(renderer);
    return true;
}

/* The other half: viewport, clear, letterbox fill. Runs once the composing
 * renderer has bound the framebuffer it drew its own conclusion about. */
void
es2_frame_surface_clear(struct TRSPK_Renderer_ES2* renderer, bool clear_to_black_only)
{
    struct ES2Rect letterbox;
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
        es2_set_scissor(renderer, &letterbox);
        glClearColor(
            (float)((TORIRS_GLES2_BG >> 16) & 0xffu) / 255.0f,
            (float)((TORIRS_GLES2_BG >> 8) & 0xffu) / 255.0f,
            (float)(TORIRS_GLES2_BG & 0xffu) / 255.0f,
            1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        es2_set_scissor(renderer, NULL);
    }
    es2_set_letterbox_viewport(renderer);
}

/* The bar's caption, through the same font path a frame uses, so a boot
 * sentence is one picture and not one per renderer. */
void
es2_draw_boot_caption(struct TRSPK_Renderer_ES2* renderer, int caption_font_id, char const* caption)
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
    es2_begin_2d(renderer);
    es2_ui_draw_font(renderer, &font_command);
    es2_end_2d(renderer);
}






/*
 * The frame that is about to be presented, sampled back onto the canvas grid.
 *
 * Two conversions: the frame is letterboxed inside the buffer it was drawn
 * into, and GL reports rows bottom-up while the client's buffers are top-down.
 * GLES2 reads GL_RGBA only, so the bytes are repacked into the ARGB words the
 * rest of the client thinks in. An offscreen frame is read from its own
 * buffer, at render resolution, not from the scaled copy on the drawable.
 */
bool
TRSPK_Renderer_ES2_ReadPixels(struct TRSPK_Renderer_ES2* renderer, int* pixels, int width, int height)
{
    int framebuffer_w = 0;
    int framebuffer_h = 0;
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
    framebuffer = (uint8_t*)malloc((size_t)framebuffer_w * (size_t)framebuffer_h * 4u);
    assert(framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    if( offscreen )
        glBindFramebuffer(GL_FRAMEBUFFER, renderer->scale_fbo);
    glReadPixels(0, 0, framebuffer_w, framebuffer_h, GL_RGBA, GL_UNSIGNED_BYTE, framebuffer);
    if( offscreen )
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    scale_x = (float)renderer->letterbox_width / (float)width;
    scale_y = (float)renderer->letterbox_height / (float)height;
    for( y = 0; y < height; y++ )
    {
        int source_y = renderer->letterbox_y + (int)((float)(height - 1 - y) * scale_y);
        int x;
        source_y = es2_clampi(source_y, 0, framebuffer_h - 1);
        for( x = 0; x < width; x++ )
        {
            int source_x = renderer->letterbox_x + (int)((float)x * scale_x);
            const uint8_t* source;
            uint32_t alpha;
            source_x = es2_clampi(source_x, 0, framebuffer_w - 1);
            source = framebuffer + ((size_t)source_y * (size_t)framebuffer_w + (size_t)source_x) * 4u;
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
TRSPK_Renderer_ES2_Name(struct TRSPK_Renderer_ES2 const* renderer)
{
    assert(renderer);
    return renderer->name;
}
