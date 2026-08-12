#include "platform/platform_sdl2_renderer_gl3.h"

#include "core/trspk_atlas.h"
#include "core/trspk_drawrangeex.h"
#include "core/trspk_drawrangelist.h"
#include "core/trspk_ibo.h"
#include "core/trspk_modelarena.h"
#include "core/trspk_pose.h"
#include "core/trspk_triangles.h"
#include "core/trspk_vbo.h"
#if defined(TORIRS_GL_ES2)
#include "webgl1/trspk_webgl1.h"
#else
#include "opengl3/opengl3_2d_shaders.h"
#include "opengl3/opengl3_sdlgl.h"
#include "opengl3/trspk_opengl3.h"
#endif
#include "perf/torirs_perf.h"
#include "render/torirs_frame.h"
#include "render/torirs_pick.h"
#include "render/trspk_sprite.h"
#include "render/trspk_toridraw.h"

#include "toridraw.h"
#include "toridraw_font.h"
#include "toridraw_math.h"
#include "toridraw_model.h"
#include "toridraw_model_sprite.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"
#include "toridraw_types.h"

#include <SDL.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TORIRS_GL3_BG 0xFF202428

/*
 * Two GPU backends, one renderer.
 *
 * Natively this is desktop GL 3.2 core; on the web it is WebGL1 (GLES2) with no
 * extensions — see 3rd/trspk/webgl1/trspk_webgl1.h for what that rules out.
 * Everything above the handful of definitions below is written once: the draw
 * order, the atlas, the sprite variants, the picking and the 2D batcher do not
 * know which context they are running on, and a fix to any of them lands on
 * both. What genuinely differs is named here.
 */
#if defined(TORIRS_GL_ES2)
/* GLES2 has no sized internal formats: internalformat must equal format. */
#define TORIRS_GL_TEX_RGBA GL_RGBA
/* ...and no single-channel red. LUMINANCE replicates into rgb, so the shaders
 * still read coverage from .r. */
#define TORIRS_GL_TEX_R GL_LUMINANCE
#define TORIRS_GL_TEX_R_FORMAT GL_LUMINANCE
/* No GL_BGRA at all, glReadPixels included; the pick path swizzles instead. */
#define TORIRS_GL_READ_FORMAT GL_RGBA
#define TORIRS_GL_BACKEND_NAME "WebGL1"
/* Index element width the GPU buffer is sized in. */
#define TORIRS_GL_INDEX_SIZE sizeof(uint16_t)
#else
#define TORIRS_GL_TEX_RGBA GL_RGBA8
#define TORIRS_GL_TEX_R GL_R8
#define TORIRS_GL_TEX_R_FORMAT GL_RED
#define TORIRS_GL_READ_FORMAT GL_BGRA
#define TORIRS_GL_BACKEND_NAME "OpenGL3"
#define TORIRS_GL_INDEX_SIZE sizeof(uint32_t)
#endif

/*
 * The two vertex layouts are the same 48 bytes in the same order (see
 * webgl1_vertex.h and opengl3_vertex.h). The CPU-side arena is built on the
 * OpenGL3 one on both paths because trspk_modelarena's compaction moves
 * vertices through that member; declaring a second identical format would only
 * give the two a chance to drift apart.
 */
_Static_assert(
    sizeof(struct TRSPK_VertexWebGL1) == sizeof(struct TRSPK_VertexOpenGl3),
    "WebGL1 and OpenGL3 vertices must stay layout-compatible");

typedef struct TRSPK_UboWorld
{
    float modelViewMatrix[16];
    float projectionMatrix[16];
    float uClock;
    float uAtlasDim;
    float uAtlasSlots;
    float _pad;
} TRSPK_UboWorld;

/* Preferred atlas: 4096^2 / 128 = 32x32 = 1024 slots. Falls back to 2048/16/256
 * when GL_MAX_TEXTURE_SIZE cannot take 4096 (see gl3_init_texture_atlas). */
#define TRSPK_GL3_ATLAS_DIM_PREF 4096u
#define TRSPK_GL3_ATLAS_COLS_PREF 32u
#define TRSPK_GL3_TEX_CAP_PREF 1024u
#define TRSPK_GL3_ATLAS_DIM_FALLBACK 2048u
#define TRSPK_GL3_ATLAS_COLS_FALLBACK 16u
#define TRSPK_GL3_TEX_CAP_FALLBACK 256u
#define TRSPK_GL3_TEX_CAP_MAX TRSPK_GL3_TEX_CAP_PREF
#define TRSPK_GL3_DRAWRANGE_CAP 4096u
/*
 * Vertex page the model arena allocates within — a slot never crosses one.
 *
 * Desktop GL indexes the arena directly with 32-bit indices, so a page boundary
 * would only waste the tail of each page and buy nothing: one page covers
 * everything.
 *
 * WebGL1 draws with 16-bit indices and has no glDrawElementsBaseVertex, so a
 * draw's vertices must lie inside one 65536-vertex window and the base is
 * folded into the attribute pointers. Paging the arena at exactly that window
 * makes the window a property of where a model was placed rather than something
 * the index splitter has to search for — which is what D3D9 has always done
 * (D3D9_VBO_PAGE = 65536, same limit, same reason).
 *
 * Measured on a settled osrs239 scene: searching produced 2878 draw calls and
 * 2878 attribute rebinds per frame from a single draw range, because painter
 * order walks the arena in a different order than it was filled. Paging is what
 * collapses that.
 */
#if defined(TORIRS_GL_ES2)
#define TRSPK_GL3_VBO_PAGE 65536u
#else
#define TRSPK_GL3_VBO_PAGE (1u << 28)
#endif
#define TRSPK_GL3_GPU_IBO_INIT 4096u
#define TRSPK_GL3_GPU_VBO_INIT 4096u
#define TRSPK_GL3_SPRITE_CAP 2048
#define TRSPK_GL3_FONT_CAP 32
#define TRSPK_GL3_2D_ATLAS_DIM 2048u
#define GL3_2D_BATCH_MAX_VERTS 32768u

struct GL3SpriteSlot
{
    int scene_id;
    int count;
    float* uvs;
    uint8_t* loaded;
};

struct GL3SpriteVariant
{
    int scene_id;
    int atlas_index;
    int outline;
    int graphic_shadow;
    int angle;
    uint8_t flip_h;
    uint8_t flip_v;
    float u0;
    float v0;
    float u1;
    float v1;
    int ox;
    int oy;
    int sw;
    int sh;
    bool valid;
};

#define GL3_SPRITE_VARIANT_CAP 2048u
#define GL3_WIDGET_MODEL_NEAR 50
/* Mid-relative widget Z spans hundreds–thousands; the 2D ortho used for UI
 * models must cover that or faces clip to nothing (chatheads look transparent). */
#define GL3_WIDGET_MODEL_Z_NEAR (-8192.0f)
#define GL3_WIDGET_MODEL_Z_FAR 8192.0f
#define GL3_FONT_BOX_MAX_LINES 64
/* Ephemeral arena key for UI MODEL widgets. The DYNAMIC group is reset before
 * each widget bake, so this never collides with world element ids. */
#define GL3_WIDGET_ARENA_ELEMENT_ID 0
/* Stable slots so compass and minimap never share a GL texture. Keyed by
 * (scene_id, mask_scene_id, dst size); content is rewritten every draw. */
#define GL3_ROTMASK_DEDICATED_CAP 8

struct GL3RotmaskDedicated
{
    int scene_id;
    int mask_scene_id;
    int dst_w;
    int dst_h;
    GLuint texture;
    int tex_w;
    int tex_h;
    bool used;
};

struct GL3FontSlot
{
    int font_id;
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

struct ToriRS_GL3
{
    struct ToriDraw_Scene* scene;
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
    /* Texture storage is allocated once; every later write is a dirty-rect
     * glTexSubImage2D. See gl3_upload_atlas_texture. */
    bool atlas_storage_ready;
    /* Tightly packed rows for that sub-upload — WebGL1 has no
     * GL_UNPACK_ROW_LENGTH, so a sub-rectangle cannot be read out of the wider
     * CPU atlas in place. Grown to the worst rectangle, never shrunk. */
    uint8_t* atlas_stage;
    size_t atlas_stage_capacity;
    uint32_t atlas_dim;
    uint32_t atlas_cols;
    uint32_t tex_cap;
    /* Cache texture id -> atlas slot. Texture ids run past 255 in RS2 materials;
     * the atlas is dense and indexed by allocated slot, not by id. */
    int tex_slot_of_id[TORIDRAW_TEXTURE_ID_CAPACITY];
    uint32_t tex_slot_next;
    /* Track which atlas slots actually have decoded texels. */
    uint8_t tex_resident[TRSPK_GL3_TEX_CAP_MAX];

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
    /* Mirror of the world uniform block, pushed per program bind on GLES2. */
    TRSPK_UboWorld ubo_cpu;
    GLint u_model_view;
    GLint u_projection;
    GLint u_clock;
    GLint u_atlas_dim;
    GLint u_atlas_slots;
#if defined(TORIRS_GL_ES2)
    /* 16-bit index staging, and the draw chunks it was split into. */
    uint16_t* idx16;
    uint32_t idx16_capacity;
    struct TRSPK_WebGL1Chunk* chunks;
    uint32_t chunk_capacity;
#endif

    float view[16];
    float proj[16];
    struct ToriRS_RenderCommand_Begin3D cur_3d;
    bool has_3d;
    bool in3d;
    double frame_clock;

    struct TRSPK_Atlas sprite_atlas;
    GLuint sprite_atlas_texture;
    GLuint white_texture;
    /* Dedicated GL textures for rotated+masked chrome (minimap, compass).
     * Soft3D reblits into the framebuffer every frame; we keep one texture per
     * (scene, mask, size) and rewrite it each draw so the two never alias. */
    struct GL3RotmaskDedicated rotmask_slots[GL3_ROTMASK_DEDICATED_CAP];
    GLuint rotmask_last_texture;
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

    bool pick_enabled;
    int pick_mouse_x;
    int pick_mouse_y;
    struct ToriRS_PickHits pick_hits;
};

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

/*
 * Point the world attributes at `vbo_gpu`, `base_vertex` vertices in.
 *
 * The base is how WebGL1 draws past index 65535 without
 * OES_element_index_uint: shifting every attribute pointer by whole vertices
 * makes 16-bit indices read from a window starting there, which is
 * glDrawElementsBaseVertex expressed in the only terms GLES2 has. The GL3 path
 * always passes 0 — it indexes the whole arena directly with 32-bit indices.
 */
static void
bind_vbo_attribs(
    struct ToriRS_GL3* renderer,
    GLuint vbo_gpu,
    uint32_t base_vertex)
{
    const GLsizei stride = (GLsizei)sizeof(struct TRSPK_VertexOpenGl3);
    const uintptr_t base = (uintptr_t)base_vertex * sizeof(struct TRSPK_VertexOpenGl3);
    const uintptr_t offset_position = base + offsetof(struct TRSPK_VertexOpenGl3, position);
    const uintptr_t offset_color = base + offsetof(struct TRSPK_VertexOpenGl3, color);
    const uintptr_t offset_texcoord = base + offsetof(struct TRSPK_VertexOpenGl3, texcoord);
    const uintptr_t offset_tex_id = base + offsetof(struct TRSPK_VertexOpenGl3, tex_id);
    const uintptr_t offset_uv_mode = base + offsetof(struct TRSPK_VertexOpenGl3, uv_mode);
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

/*
 * Vertex-array objects, or the lack of them.
 *
 * WebGL1 has no VAOs — OES_vertex_array_object is an extension — so the
 * attribute state a VAO would have captured is simply re-established at every
 * bind. These three calls stand in for glBindVertexArray everywhere so the
 * renderer never has to know which it is doing.
 */
static void
gl3_bind_group_attribs(
    struct ToriRS_GL3* renderer,
    struct GL3ModelGroup* group,
    uint32_t base_vertex)
{
#if defined(TORIRS_GL_ES2)
    bind_vbo_attribs(renderer, group->vbo_gpu, base_vertex);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
#else
    (void)base_vertex; /* GL3 indexes the arena directly with 32-bit indices. */
    (void)renderer;
    glBindVertexArray(group->vao);
#endif
}

#if defined(TORIRS_GL_ES2)
/*
 * Re-base the attribute pointers, and nothing else.
 *
 * The world draw loop calls this thousands of times a frame — once per chunk,
 * and a chunk is about one visible model — so what it does NOT do matters more
 * than what it does. gl3_bind_group_attribs above issues twelve GL calls: an
 * array-buffer bind, five glEnableVertexAttribArray, five
 * glVertexAttribPointer, and an element-buffer bind. Only the five pointers
 * carry the base vertex; the rest are the same values they already held.
 *
 * That is eleven redundant calls per model, and on this host a GL call is a
 * crossing out of wasm into JavaScript. At 2,878 chunks a frame it was ~37,400
 * calls where ~17,300 are needed.
 *
 * So the loop hoists the buffer binds and the enables and calls this instead.
 * It is deliberately not a general-purpose state cache: the caller guarantees
 * nothing between draws touches buffer bindings or array enables, which is true
 * of the world loop and would not be true anywhere else.
 */
static void
gl3_point_group_attribs(
    struct ToriRS_GL3* renderer,
    uint32_t base_vertex)
{
    const GLsizei stride = (GLsizei)sizeof(struct TRSPK_VertexOpenGl3);
    const uintptr_t base = (uintptr_t)base_vertex * sizeof(struct TRSPK_VertexOpenGl3);

    if( renderer->a_position >= 0 )
        glVertexAttribPointer(
            (GLuint)renderer->a_position, 4, GL_FLOAT, GL_FALSE, stride,
            (const void*)(base + offsetof(struct TRSPK_VertexOpenGl3, position)));
    if( renderer->a_color >= 0 )
        glVertexAttribPointer(
            (GLuint)renderer->a_color, 4, GL_FLOAT, GL_FALSE, stride,
            (const void*)(base + offsetof(struct TRSPK_VertexOpenGl3, color)));
    if( renderer->a_texcoord >= 0 )
        glVertexAttribPointer(
            (GLuint)renderer->a_texcoord, 2, GL_FLOAT, GL_FALSE, stride,
            (const void*)(base + offsetof(struct TRSPK_VertexOpenGl3, texcoord)));
    if( renderer->a_tex_id >= 0 )
        glVertexAttribPointer(
            (GLuint)renderer->a_tex_id, 1, GL_FLOAT, GL_FALSE, stride,
            (const void*)(base + offsetof(struct TRSPK_VertexOpenGl3, tex_id)));
    if( renderer->a_uv_mode >= 0 )
        glVertexAttribPointer(
            (GLuint)renderer->a_uv_mode, 1, GL_FLOAT, GL_FALSE, stride,
            (const void*)(base + offsetof(struct TRSPK_VertexOpenGl3, uv_mode)));
}
#endif

static void
gl3_bind_quad_attribs(struct ToriRS_GL3* renderer)
{
#if defined(TORIRS_GL_ES2)
    glBindBuffer(GL_ARRAY_BUFFER, renderer->quad_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct GL3Vertex2D), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, sizeof(struct GL3Vertex2D), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2, 4, GL_FLOAT, GL_FALSE, sizeof(struct GL3Vertex2D), (void*)(4 * sizeof(float)));
#else
    glBindVertexArray(renderer->quad_vao);
#endif
}

static void
gl3_unbind_attribs(struct ToriRS_GL3* renderer)
{
#if defined(TORIRS_GL_ES2)
    (void)renderer;
    /* Attribute arrays are global state here; leaving them enabled would make
     * the next program read whatever buffer happened to be bound. */
    for( GLuint i = 0u; i < 5u; ++i )
        glDisableVertexAttribArray(i);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
#else
    (void)renderer;
    glBindVertexArray(0);
#endif
}

/*
 * World uniforms.
 *
 * GL3 keeps them in a std140 uniform block; GLES2 has no uniform blocks, so
 * they are plain uniforms pushed while the program is current. Both paths
 * write the same CPU-side struct and both push it from the same place, which
 * is what keeps a matrix change from landing on one backend only.
 */
static void
gl3_set_world_uniforms(
    struct ToriRS_GL3* renderer,
    const TRSPK_UboWorld* u)
{
    renderer->ubo_cpu = *u;
#if !defined(TORIRS_GL_ES2)
    glBindBuffer(GL_UNIFORM_BUFFER, renderer->ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)sizeof(TRSPK_UboWorld), u);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    glBindBufferRange(GL_UNIFORM_BUFFER, 0u, renderer->ubo, 0, (GLsizeiptr)sizeof(TRSPK_UboWorld));
#endif
}

/** Make the current world uniforms visible to program3d (must be current). */
static void
gl3_push_world_uniforms(struct ToriRS_GL3* renderer)
{
#if defined(TORIRS_GL_ES2)
    if( renderer->u_model_view >= 0 )
        glUniformMatrix4fv(
            renderer->u_model_view, 1, GL_FALSE, renderer->ubo_cpu.modelViewMatrix);
    if( renderer->u_projection >= 0 )
        glUniformMatrix4fv(
            renderer->u_projection, 1, GL_FALSE, renderer->ubo_cpu.projectionMatrix);
    if( renderer->u_clock >= 0 )
        glUniform1f(renderer->u_clock, renderer->ubo_cpu.uClock);
    if( renderer->u_atlas_dim >= 0 )
        glUniform1f(renderer->u_atlas_dim, renderer->ubo_cpu.uAtlasDim);
    if( renderer->u_atlas_slots >= 0 )
        glUniform1f(renderer->u_atlas_slots, renderer->ubo_cpu.uAtlasSlots);
#else
    glBindBufferRange(
        GL_UNIFORM_BUFFER, 0u, renderer->ubo, 0, (GLsizeiptr)sizeof(TRSPK_UboWorld));
#endif
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
gl3_destroy_gl_resources(struct ToriRS_GL3* renderer)
{
    if( renderer->program3d )
        glDeleteProgram(renderer->program3d);
    if( renderer->ubo )
        glDeleteBuffers(1, &renderer->ubo);
    if( renderer->ebo )
        glDeleteBuffers(1, &renderer->ebo);
#if defined(TORIRS_GL_ES2)
    free(renderer->idx16);
    renderer->idx16 = NULL;
    renderer->idx16_capacity = 0u;
    free(renderer->chunks);
    renderer->chunks = NULL;
    renderer->chunk_capacity = 0u;
#endif
    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        struct GL3ModelGroup* g = &renderer->groups[gi];
        if( g->vao )
#if !defined(TORIRS_GL_ES2)
            glDeleteVertexArrays(1, &g->vao);
#endif
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
    for( int i = 0; i < GL3_ROTMASK_DEDICATED_CAP; i++ )
    {
        if( renderer->rotmask_slots[i].texture )
            glDeleteTextures(1, &renderer->rotmask_slots[i].texture);
        memset(&renderer->rotmask_slots[i], 0, sizeof(renderer->rotmask_slots[i]));
    }
    renderer->rotmask_last_texture = 0u;
    if( renderer->white_texture )
        glDeleteTextures(1, &renderer->white_texture);
    if( renderer->quad_vao )
#if !defined(TORIRS_GL_ES2)
        glDeleteVertexArrays(1, &renderer->quad_vao);
#endif
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
    renderer->white_texture = 0u;
}

static void
gl3_upload_sprite_atlas(struct ToriRS_GL3* renderer)
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
            TORIRS_GL_TEX_RGBA,
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
    struct ToriRS_GL3* renderer,
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
    struct ToriRS_GL3* renderer,
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
    /* Letterbox scale can truncate thin logical rects to 0px; OpenGL accepts a
     * zero-size scissor (nothing draws) which Soft3D would still partially fill. */
    if( logical_w > 0 && gl_w < 1 )
        gl_w = 1;
    if( logical_h > 0 && gl_h < 1 )
        gl_h = 1;
    glEnable(GL_SCISSOR_TEST);
    glScissor(gl_x, gl_y, gl_w, gl_h);
}

static int
gl3_text_debug_enabled(void)
{
    static int cached = -1;
    if( cached < 0 )
        cached = getenv("TORIRS_GL3_TEXT_DEBUG") != NULL;
    return cached;
}

static void
gl3_flush_2d_batch(struct ToriRS_GL3* renderer);

static void
gl3_batch2d_flush_if_needed(
    struct ToriRS_GL3* renderer,
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
    struct ToriRS_GL3* renderer,
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
    struct ToriRS_GL3* renderer,
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
gl3_batch2d_reset(struct ToriRS_GL3* renderer)
{
    struct GL3Batch2DState* b = &renderer->batch2d;
    b->vert_count = 0u;
    b->texture = 0u;
    b->text_mode = 0;
    b->uv_clamp = false;
    b->scissor_set = false;
}

static void
gl3_batch2d_free(struct ToriRS_GL3* renderer)
{
    free(renderer->batch2d.verts);
    renderer->batch2d.verts = NULL;
    renderer->batch2d.vert_count = 0u;
}

static bool
gl3_batch2d_init(struct ToriRS_GL3* renderer)
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
gl3_flush_2d_batch(struct ToriRS_GL3* renderer);

static void
gl3_batch2d_flush_if_needed(
    struct ToriRS_GL3* renderer,
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
    struct ToriRS_GL3* renderer,
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
gl3_flush_2d_batch(struct ToriRS_GL3* renderer)
{
    struct GL3Batch2DState* b = &renderer->batch2d;
    if( b->vert_count == 0u )
        return;

    if( b->scissor_set )
    {
        if( gl3_text_debug_enabled() && b->text_mode )
        {
            int gl_x = 0;
            int gl_y = 0;
            int gl_w = 0;
            int gl_h = 0;
            trspk_logical_rect_to_framebuffer(
                b->scissor_x,
                b->scissor_y,
                b->scissor_w,
                b->scissor_h,
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
            fprintf(
                stderr,
                "gl3_flush_2d_batch: text verts=%u tex=%u scissor_log=%d,%d %dx%d "
                "scissor_fb=%d,%d %dx%d lb=%d,%d %dx%d\n",
                b->vert_count,
                (unsigned)b->texture,
                b->scissor_x,
                b->scissor_y,
                b->scissor_w,
                b->scissor_h,
                gl_x,
                gl_y,
                gl_w,
                gl_h,
                renderer->lb_x,
                renderer->lb_y,
                renderer->lb_w,
                renderer->lb_h);
        }
        gl3_apply_logical_scissor(renderer, b->scissor_x, b->scissor_y, b->scissor_w, b->scissor_h);
    }
    else
        glDisable(GL_SCISSOR_TEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(renderer->program2d);
    glUniformMatrix4fv(renderer->u2d_projection, 1, GL_FALSE, renderer->proj2d);
    gl3_bind_quad_attribs(renderer);
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

    if( gl3_text_debug_enabled() && b->text_mode )
        gl3_check_error("2d flush text");

    b->vert_count = 0u;
}

static void
gl3_draw_textured_quad_immediate(
    struct ToriRS_GL3* renderer,
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
    struct ToriRS_GL3* renderer,
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
    struct ToriRS_GL3* renderer,
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


static void
gl3_draw_sprite_rotated(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command,
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
        command->u.sprite.rotation_r2pi2048,
        u0,
        v0,
        u1,
        v1,
        pos,
        uv);

    gl3_draw_textured_quad_uv4(renderer, pos, uv, rgba);
}


static int
gl3_sprite_slot_index(
    struct ToriRS_GL3* renderer,
    int scene_id,
    bool create)
{
    int free_idx = -1;
    for( int i = 0; i < TRSPK_GL3_SPRITE_CAP; i++ )
    {
        if( renderer->sprite_slots[i].scene_id == scene_id )
            return i;
        if( free_idx < 0 && renderer->sprite_slots[i].scene_id <= 0 )
            free_idx = i;
    }
    if( !create || free_idx < 0 )
    {
        if( create && free_idx < 0 )
        {
            static int warned = 0;
            if( !warned )
            {
                warned = 1;
                fprintf(
                    stderr,
                    "gl3: 2D sprite slot table full (%d entries); further scene "
                    "sprites dropped\n",
                    TRSPK_GL3_SPRITE_CAP);
            }
        }
        return -1;
    }
    renderer->sprite_slots[free_idx].scene_id = scene_id;
    renderer->sprite_slots[free_idx].count = 0;
    free(renderer->sprite_slots[free_idx].uvs);
    free(renderer->sprite_slots[free_idx].loaded);
    renderer->sprite_slots[free_idx].uvs = NULL;
    renderer->sprite_slots[free_idx].loaded = NULL;
    return free_idx;
}

static void
gl3_scale_pixel_alpha(
    uint32_t* buf,
    size_t count,
    int alpha)
{
    size_t i;
    if( !buf || alpha >= 255 )
        return;
    if( alpha < 0 )
        alpha = 0;
    for( i = 0; i < count; i++ )
    {
        uint32_t p = buf[i];
        int a = (int)((p >> 24) & 0xFF);
        a = (a * alpha) / 255;
        buf[i] = (p & 0x00FFFFFFu) | ((uint32_t)a << 24);
    }
}

/*
 * ToriDraw sprite pixels -> what GL uploads.
 *
 * Two conversions, and skipping either is invisible in a software rasterizer
 * and fatal here. A ToriDraw pixel is an ARGB int, which little-endian memory
 * lays out as B,G,R,A — GL_RGBA wants R,G,B,A, so the channels swap. And the
 * alpha is a *convention*, not a value: much of what the client bakes (the
 * minimap pixmap among it) carries alpha 0 throughout, because Soft3D writes
 * into an opaque framebuffer and never reads it back. An explicit alpha wins;
 * a pixel with none is opaque unless it is fully black, which is the
 * transparent key.
 *
 * Uploading such a buffer raw gives a texture that is entirely transparent and
 * has red and blue swapped — which is exactly what the minimap did: its ground
 * vanished under the 2D shader's alpha discard while the overlay dots, which
 * come through the path below that does convert, kept drawing.
 *
 * Safe in place (src == dst).
 */
static void
gl3_argb_to_rgba(
    uint32_t const* src,
    uint32_t* dst,
    size_t count)
{
    for( size_t i = 0; i < count; i++ )
    {
        uint32_t const pix = src[i];
        uint8_t const a_hi = (uint8_t)((pix >> 24) & 0xFFu);
        uint32_t const rgb = pix & 0x00FFFFFFu;
        uint8_t const a = (a_hi != 0u) ? a_hi : (rgb != 0u ? 0xFFu : 0u);
        dst[i] = (uint32_t)((pix >> 16) & 0xFFu) | ((uint32_t)((pix >> 8) & 0xFFu) << 8) |
                 ((uint32_t)(pix & 0xFFu) << 16) | ((uint32_t)a << 24);
    }
}

static uint32_t*
gl3_clamp_to_nominal(
    uint32_t const* src,
    int src_w,
    int src_h,
    int src_ox,
    int src_oy,
    int nominal_w,
    int nominal_h)
{
    uint32_t* dst;
    int y;
    int x;
    if( !src || nominal_w <= 0 || nominal_h <= 0 || src_w <= 0 || src_h <= 0 )
        return NULL;
    dst = calloc((size_t)nominal_w * (size_t)nominal_h, sizeof(uint32_t));
    if( !dst )
        return NULL;
    for( y = 0; y < src_h; y++ )
    {
        int dst_y = y + src_oy;
        if( dst_y < 0 || dst_y >= nominal_h )
            continue;
        for( x = 0; x < src_w; x++ )
        {
            int dst_x = x + src_ox;
            if( dst_x < 0 || dst_x >= nominal_w )
                continue;
            dst[dst_y * nominal_w + dst_x] = src[y * src_w + x];
        }
    }
    return dst;
}

static bool
gl3_sprite_upload_rgba(
    struct ToriRS_GL3* renderer,
    uint8_t const* crop_pixels,
    uint32_t src_stride,
    int upload_w,
    int upload_h,
    float* out_uv)
{
    struct TRSPK_AtlasTile tile;
    if( !trspk_atlas_binpack_insert(
            &renderer->sprite_atlas,
            crop_pixels,
            src_stride,
            (uint32_t)upload_w,
            (uint32_t)upload_h,
            &tile) )
        return false;
    out_uv[0] = tile.u_start;
    out_uv[1] = tile.v_start;
    out_uv[2] = tile.u_end;
    out_uv[3] = tile.v_end;
    gl3_upload_sprite_atlas(renderer);
    return true;
}

static bool
gl3_sprite_prepare_pixels(
    struct ToriDraw_Sprite* sp,
    uint32_t** out_px,
    int* out_w,
    int* out_h,
    int* out_ox,
    int* out_oy)
{
    int sw;
    int sh;
    int ox;
    int oy;
    size_t pixel_count;
    uint32_t* spr_px;
    if( !sp || !sp->pixels_argb || sp->width <= 0 || sp->height <= 0 )
        return false;
    sw = sp->width;
    sh = sp->height;
    ox = sp->crop_x;
    oy = sp->crop_y;
    pixel_count = (size_t)sw * (size_t)sh;
    spr_px = malloc(pixel_count * sizeof(uint32_t));
    if( !spr_px )
        return false;
    memcpy(spr_px, sp->pixels_argb, pixel_count * sizeof(uint32_t));
    *out_px = spr_px;
    *out_w = sw;
    *out_h = sh;
    *out_ox = ox;
    *out_oy = oy;
    return true;
}

static struct GL3SpriteVariant*
gl3_sprite_find_variant(
    int scene_id,
    int atlas_index,
    int outline,
    int graphic_shadow,
    int angle,
    uint8_t flip_h,
    uint8_t flip_v,
    bool create)
{
    static struct GL3SpriteVariant variants[GL3_SPRITE_VARIANT_CAP];
    static bool inited = false;
    if( !inited )
    {
        memset(variants, 0, sizeof(variants));
        inited = true;
    }
    for( size_t i = 0; i < GL3_SPRITE_VARIANT_CAP; i++ )
    {
        struct GL3SpriteVariant* v = &variants[i];
        if( !v->valid )
        {
            if( !create )
                return NULL;
            memset(v, 0, sizeof(*v));
            v->scene_id = scene_id;
            v->atlas_index = atlas_index;
            v->outline = outline;
            v->graphic_shadow = graphic_shadow;
            v->angle = angle;
            v->flip_h = flip_h;
            v->flip_v = flip_v;
            return v;
        }
        if( v->scene_id == scene_id && v->atlas_index == atlas_index && v->outline == outline &&
            v->graphic_shadow == graphic_shadow && v->angle == angle && v->flip_h == flip_h &&
            v->flip_v == flip_v )
            return v;
    }
    return NULL;
}

static bool
gl3_sprite_ensure_base(
    struct ToriRS_GL3* renderer,
    int scene_id,
    int atlas_index,
    struct ToriDraw_Sprite** out_sprite,
    float* out_uv)
{
    struct ToriDraw_Sprite** sprites = NULL;
    int count = 0;
    int slot_i;
    struct GL3SpriteSlot* slot;
    struct ToriDraw_Sprite* sp;
    if( scene_id <= 0 || !renderer->scene )
        return false;
    sprites = ToriDraw_SceneSpriteGet(renderer->scene, scene_id, &count);
    if( !sprites || atlas_index < 0 || atlas_index >= count )
        return false;
    sp = sprites[atlas_index];
    if( !sp || !sp->pixels_argb )
        return false;
    slot_i = gl3_sprite_slot_index(renderer, scene_id, true);
    if( slot_i < 0 )
        return false;
    slot = &renderer->sprite_slots[slot_i];
    if( slot->count != count )
    {
        slot->count = count;
        free(slot->uvs);
        free(slot->loaded);
        slot->uvs = calloc((size_t)count * 4u, sizeof(float));
        slot->loaded = calloc((size_t)count, sizeof(uint8_t));
        if( !slot->uvs || !slot->loaded )
            return false;
    }
    if( slot->loaded[atlas_index] )
    {
        *out_sprite = sp;
        out_uv[0] = slot->uvs[atlas_index * 4 + 0];
        out_uv[1] = slot->uvs[atlas_index * 4 + 1];
        out_uv[2] = slot->uvs[atlas_index * 4 + 2];
        out_uv[3] = slot->uvs[atlas_index * 4 + 3];
        return true;
    }
    {
        int upload_x = 0;
        int upload_y = 0;
        int upload_w = sp->width;
        int upload_h = sp->height;
        uint32_t* rgba = malloc((size_t)sp->width * (size_t)sp->height * sizeof(uint32_t));
        float uv[4];
        if( !rgba )
            return false;
        gl3_argb_to_rgba(
            (uint32_t const*)sp->pixels_argb, rgba, (size_t)sp->width * (size_t)sp->height);
        if( sp->crop_width > 0 &&
            (sp->crop_width < sp->width || sp->crop_height < sp->height) )
        {
            upload_x = sp->crop_x;
            upload_y = sp->crop_y;
            upload_w = sp->crop_width;
            upload_h = sp->crop_height;
        }
        if( !gl3_sprite_upload_rgba(
                renderer,
                (uint8_t const*)rgba +
                    ((size_t)upload_y * (size_t)sp->width + (size_t)upload_x) * 4u,
                (uint32_t)sp->width * 4u,
                upload_w,
                upload_h,
                uv) )
        {
            free(rgba);
            return false;
        }
        free(rgba);
        slot->uvs[atlas_index * 4 + 0] = uv[0];
        slot->uvs[atlas_index * 4 + 1] = uv[1];
        slot->uvs[atlas_index * 4 + 2] = uv[2];
        slot->uvs[atlas_index * 4 + 3] = uv[3];
        slot->loaded[atlas_index] = 1u;
    }
    *out_sprite = sp;
    out_uv[0] = slot->uvs[atlas_index * 4 + 0];
    out_uv[1] = slot->uvs[atlas_index * 4 + 1];
    out_uv[2] = slot->uvs[atlas_index * 4 + 2];
    out_uv[3] = slot->uvs[atlas_index * 4 + 3];
    return true;
}

static bool
gl3_sprite_ensure_variant(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand_Sprite const* cmd,
    struct ToriDraw_Sprite** out_sprite,
    float* out_uv,
    int* out_ox,
    int* out_oy,
    int* out_sw,
    int* out_sh)
{
    struct ToriDraw_Sprite* sp = NULL;
    float base_uv[4];
    struct GL3SpriteVariant* variant;
    uint32_t* spr_px = NULL;
    int sw;
    int sh;
    int ox;
    int oy;
    int nominal_w;
    int nominal_h;
    int alpha;
    if( !gl3_sprite_ensure_base(renderer, cmd->scene_id, cmd->atlas_index, &sp, base_uv) )
        return false;
    if( cmd->outline <= 0 && cmd->graphic_shadow == 0 && cmd->trans <= 0 && !cmd->flip_h &&
        !cmd->flip_v && cmd->sprite_angle_r2pi65536 == 0 )
    {
        *out_sprite = sp;
        out_uv[0] = base_uv[0];
        out_uv[1] = base_uv[1];
        out_uv[2] = base_uv[2];
        out_uv[3] = base_uv[3];
        *out_ox = sp->crop_x;
        *out_oy = sp->crop_y;
        *out_sw = sp->width;
        *out_sh = sp->height;
        return true;
    }
    variant = gl3_sprite_find_variant(
        cmd->scene_id,
        cmd->atlas_index,
        cmd->outline,
        cmd->graphic_shadow,
        cmd->sprite_angle_r2pi65536,
        cmd->flip_h,
        cmd->flip_v,
        true);
    if( !variant )
        return false;
    if( variant->valid )
    {
        *out_sprite = sp;
        out_uv[0] = variant->u0;
        out_uv[1] = variant->v0;
        out_uv[2] = variant->u1;
        out_uv[3] = variant->v1;
        *out_ox = variant->ox;
        *out_oy = variant->oy;
        *out_sw = variant->sw;
        *out_sh = variant->sh;
        return true;
    }
    if( !gl3_sprite_prepare_pixels(sp, &spr_px, &sw, &sh, &ox, &oy) )
        return false;
    nominal_w = sp->width;
    nominal_h = sp->height;
    if( cmd->outline > 0 )
    {
        int sw2 = 0;
        int sh2 = 0;
        uint32_t* outlined =
            ToriDraw_SpriteNewGraphicOutline(spr_px, sw, sh, cmd->outline, &sw2, &sh2);
        if( outlined )
        {
            free(spr_px);
            spr_px = outlined;
            sw = sw2;
            sh = sh2;
        }
    }
    if( cmd->graphic_shadow != 0 )
    {
        int sw2 = 0;
        int sh2 = 0;
        uint32_t* shadowed =
            ToriDraw_SpriteNewGraphicShadow(spr_px, sw, sh, cmd->graphic_shadow, &sw2, &sh2);
        if( shadowed )
        {
            free(spr_px);
            spr_px = shadowed;
            sw = sw2;
            sh = sh2;
        }
    }
    alpha = 255 - cmd->trans;
    gl3_scale_pixel_alpha(spr_px, (size_t)sw * (size_t)sh, alpha);
    if( cmd->if3 && !cmd->tiled )
    {
        ToriDraw_SpriteTransformPixels(&spr_px, &sw, &sh, cmd->flip_h, cmd->flip_v, 0);
        if( ox != 0 || oy != 0 || sw != nominal_w || sh != nominal_h )
        {
            uint32_t* clamped =
                gl3_clamp_to_nominal(spr_px, sw, sh, ox, oy, nominal_w, nominal_h);
            if( clamped )
            {
                free(spr_px);
                spr_px = clamped;
                sw = nominal_w;
                sh = nominal_h;
                ox = 0;
                oy = 0;
            }
        }
        ToriDraw_SpriteTransformPixels(&spr_px, &sw, &sh, 0, 0, cmd->sprite_angle_r2pi65536);
    }
    else
    {
        ToriDraw_SpriteTransformPixels(
            &spr_px, &sw, &sh, cmd->flip_h, cmd->flip_v, cmd->sprite_angle_r2pi65536);
    }
    {
        float uv[4];
        /* Transforms leave ToriDraw ARGB in spr_px; GL_RGBA wants R,G,B,A. */
        gl3_argb_to_rgba(spr_px, spr_px, (size_t)sw * (size_t)sh);
        if( !gl3_sprite_upload_rgba(
                renderer, (uint8_t const*)spr_px, (uint32_t)sw * 4u, sw, sh, uv) )
        {
            free(spr_px);
            return false;
        }
        variant->u0 = uv[0];
        variant->v0 = uv[1];
        variant->u1 = uv[2];
        variant->v1 = uv[3];
        variant->ox = ox;
        variant->oy = oy;
        variant->sw = sw;
        variant->sh = sh;
        variant->valid = true;
        *out_sprite = sp;
        out_uv[0] = uv[0];
        out_uv[1] = uv[1];
        out_uv[2] = uv[2];
        out_uv[3] = uv[3];
        *out_ox = ox;
        *out_oy = oy;
        *out_sw = sw;
        *out_sh = sh;
    }
    free(spr_px);
    return true;
}

static struct GL3RotmaskDedicated*
gl3_rotmask_dedicated_slot(
    struct ToriRS_GL3* renderer,
    int scene_id,
    int mask_scene_id,
    int dst_w,
    int dst_h)
{
    int free_i = -1;
    for( int i = 0; i < GL3_ROTMASK_DEDICATED_CAP; i++ )
    {
        struct GL3RotmaskDedicated* s = &renderer->rotmask_slots[i];
        if( s->used && s->scene_id == scene_id && s->mask_scene_id == mask_scene_id &&
            s->dst_w == dst_w && s->dst_h == dst_h )
            return s;
        if( free_i < 0 && !s->used )
            free_i = i;
    }
    if( free_i < 0 )
    {
        /* Table full: steal slot 0. Compass + minimap only need two; this is
         * a backstop so a size change cannot permanently drop a draw. */
        free_i = 0;
        if( renderer->rotmask_slots[0].texture )
            glDeleteTextures(1, &renderer->rotmask_slots[0].texture);
    }
    {
        struct GL3RotmaskDedicated* s = &renderer->rotmask_slots[free_i];
        memset(s, 0, sizeof(*s));
        s->scene_id = scene_id;
        s->mask_scene_id = mask_scene_id;
        s->dst_w = dst_w;
        s->dst_h = dst_h;
        s->used = true;
        return s;
    }
}

static bool
gl3_rotmask_upload_to_slot(
    struct GL3RotmaskDedicated* slot,
    uint8_t const* rgba,
    int w,
    int h)
{
    if( !slot || !rgba || w <= 0 || h <= 0 )
        return false;
    if( !slot->texture )
        glGenTextures(1, &slot->texture);
    if( !slot->texture )
        return false;
    glBindTexture(GL_TEXTURE_2D, slot->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if( slot->tex_w != w || slot->tex_h != h )
    {
        glTexImage2D(
            GL_TEXTURE_2D, 0, TORIRS_GL_TEX_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
            rgba);
        slot->tex_w = w;
        slot->tex_h = h;
    }
    else
    {
        glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

static bool
gl3_sprite_ensure_rotated_masked(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand_Sprite const* cmd,
    struct ToriDraw_Sprite* sp,
    float* out_uv,
    GLuint* out_texture)
{
    struct ToriDraw_Sprite* mask_sp = NULL;
    struct GL3RotmaskDedicated* slot;
    int dst_w;
    int dst_h;
    uint32_t* scratch = NULL;
    struct ToriDraw_ViewPort vp = { 0 };
    if( !out_texture || cmd->scene_id <= 0 || cmd->mask_scene_id <= 0 || !renderer->scene || !sp )
        return false;
    dst_w = cmd->w > 0 ? cmd->w : sp->width;
    dst_h = cmd->h > 0 ? cmd->h : sp->height;
    if( dst_w <= 0 || dst_h <= 0 )
        return false;
    {
        int mask_count = 0;
        struct ToriDraw_Sprite** mask_sprites =
            ToriDraw_SceneSpriteGet(renderer->scene, cmd->mask_scene_id, &mask_count);
        if( !mask_sprites || cmd->mask_atlas_index < 0 || cmd->mask_atlas_index >= mask_count )
            return false;
        mask_sp = mask_sprites[cmd->mask_atlas_index];
    }
    if( !mask_sp || !mask_sp->pixels_argb )
        return false;
    slot = gl3_rotmask_dedicated_slot(
        renderer, cmd->scene_id, cmd->mask_scene_id, dst_w, dst_h);
    if( !slot )
        return false;
    scratch = calloc((size_t)dst_w * (size_t)dst_h, sizeof(uint32_t));
    if( !scratch )
        return false;
    vp.width = dst_w;
    vp.height = dst_h;
    vp.clip_left = 0;
    vp.clip_top = 0;
    vp.clip_right = dst_w;
    vp.clip_bottom = dst_h;
    vp.stride = dst_w;
    ToriDraw2D_BlitSpriteRotatedMaskedEx(
        sp,
        mask_sp,
        cmd->mask_keep_opaque,
        &vp,
        0,
        0,
        dst_w,
        dst_h,
        cmd->dst_anchor_x,
        cmd->dst_anchor_y,
        cmd->src_anchor_x,
        cmd->src_anchor_y,
        cmd->rotation_r2pi2048,
        (int*)scratch);
    /* The blit leaves ToriDraw ARGB in the scratch, so it needs the same
     * conversion every other upload gets. Without it the minimap's ground is
     * transparent and its colours are channel-swapped. */
    gl3_argb_to_rgba(scratch, scratch, (size_t)dst_w * (size_t)dst_h);
    if( !gl3_rotmask_upload_to_slot(slot, (uint8_t const*)scratch, dst_w, dst_h) )
    {
        free(scratch);
        return false;
    }
    free(scratch);
    out_uv[0] = 0.0f;
    out_uv[1] = 0.0f;
    out_uv[2] = 1.0f;
    out_uv[3] = 1.0f;
    *out_texture = slot->texture;
    renderer->rotmask_last_texture = slot->texture;
    return gl3_check_error("rotmask upload");
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
            TORIRS_GL_TEX_R,
            atlas_w,
            atlas_h,
            0,
            TORIRS_GL_TEX_R_FORMAT,
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
    if( gl3_text_debug_enabled() )
    {
        size_t nonzero = 0u;
        for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
        {
            int const gw = font->glyph_width[i];
            int const gh = font->glyph_height[i];
            uint8_t const* src = font->glyph_alpha[i];
            if( !src || gw <= 0 || gh <= 0 )
                continue;
            for( int p = 0; p < gw * gh; p++ )
                if( src[p] )
                    nonzero++;
        }
        fprintf(
            stderr,
            "gl3_bake_font_atlas: font_id=%d tex=%u atlas=%dx%d glyph_alpha_nz=%zu "
            "line_height=%d\n",
            font_id,
            (unsigned)tex,
            atlas_w,
            atlas_h,
            nonzero,
            font->line_height);
    }
    return true;
}



static int
gl3_font_slot_index(
    struct ToriRS_GL3* renderer,
    int font_id,
    bool create)
{
    int free_idx = -1;
    if( font_id < 0 )
        return -1;
    for( int i = 0; i < TRSPK_GL3_FONT_CAP; i++ )
    {
        if( renderer->font_slots[i].font_id == font_id )
            return i;
        if( free_idx < 0 && renderer->font_slots[i].font_id < 0 )
            free_idx = i;
    }
    if( !create || free_idx < 0 )
        return -1;
    renderer->font_slots[free_idx].font_id = font_id;
    renderer->font_slots[free_idx].font = NULL;
    renderer->font_slots[free_idx].baked = false;
    renderer->font_slots[free_idx].atlas_w = 0;
    renderer->font_slots[free_idx].atlas_h = 0;
    if( renderer->font_slots[free_idx].texture )
    {
        glDeleteTextures(1, &renderer->font_slots[free_idx].texture);
        renderer->font_slots[free_idx].texture = 0u;
    }
    memset(renderer->font_slots[free_idx].glyph_uv, 0, sizeof(renderer->font_slots[free_idx].glyph_uv));
    return free_idx;
}

static struct GL3FontSlot*
gl3_ensure_font_slot(
    struct ToriRS_GL3* renderer,
    int font_id)
{
    int slot_i = gl3_font_slot_index(renderer, font_id, true);
    if( slot_i < 0 )
        return NULL;

    struct GL3FontSlot* slot = &renderer->font_slots[slot_i];
    if( !slot->font )
    {
        struct ToriDraw_Scene* scene = renderer->scene;
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

static bool
gl3_font_char_eq_icase(
    char a,
    char b)
{
    if( a >= 'A' && a <= 'Z' )
        a = (char)(a + ('a' - 'A'));
    if( b >= 'A' && b <= 'Z' )
        b = (char)(b + ('a' - 'A'));
    return a == b;
}

static bool
gl3_font_line_break_at(
    char const* p,
    int* advance_out)
{
    if( !p || p[0] == '\0' )
        return false;
    if( p[0] == '\\' && p[1] == 'n' )
    {
        *advance_out = 2;
        return true;
    }
    if( p[0] == '\r' && p[1] == '\n' )
    {
        *advance_out = 2;
        return true;
    }
    if( p[0] == '\n' || p[0] == '\r' )
    {
        *advance_out = 1;
        return true;
    }
    if( p[0] == '<' && gl3_font_char_eq_icase(p[1], 'b') && gl3_font_char_eq_icase(p[2], 'r') &&
        p[3] == '/' && gl3_font_char_eq_icase(p[4], '>') )
    {
        *advance_out = 5;
        return true;
    }
    if( p[0] == '<' && gl3_font_char_eq_icase(p[1], 'b') && gl3_font_char_eq_icase(p[2], 'r') &&
        p[3] == '>' )
    {
        *advance_out = 4;
        return true;
    }
    return false;
}

static char const*
gl3_font_next_line(
    char const* rest,
    int* line_len_out,
    int* break_advance_out)
{
    char const* p = rest;
    while( p[0] != '\0' )
    {
        if( gl3_font_line_break_at(p, break_advance_out) )
        {
            *line_len_out = (int)(p - rest);
            return p;
        }
        p++;
    }
    *line_len_out = (int)(p - rest);
    *break_advance_out = 0;
    return p;
}

static bool
gl3_font_is_space(unsigned char ch)
{
    return ch == ' ' || ch == '|';
}

static int
gl3_font_measure_substring(
    struct ToriDraw_Font* font,
    char const* text,
    int len)
{
    char tmp[4096];
    if( len <= 0 )
        return 0;
    if( len >= (int)sizeof(tmp) )
        len = (int)sizeof(tmp) - 1;
    memcpy(tmp, text, (size_t)len);
    tmp[len] = '\0';
    return ToriDraw2D_MeasureString(font, tmp);
}

static void
gl3_font_vertical_metrics(
    struct ToriDraw_Font const* font,
    int* max_ascent_out,
    int* max_descent_out)
{
    int const fallback_lh = font->line_height > 0 ? font->line_height : 1;
    int min_oy = 0;
    int max_bottom = 0;
    bool any = false;

    for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
    {
        if( font->glyph_width[i] <= 0 || font->glyph_height[i] <= 0 ||
            !font->glyph_alpha[i] )
            continue;
        int const oy = font->offset_y[i];
        int const bottom = oy + font->glyph_height[i];
        if( !any || oy < min_oy )
            min_oy = oy;
        if( !any || bottom > max_bottom )
            max_bottom = bottom;
        any = true;
    }
    if( !any )
    {
        *max_ascent_out = fallback_lh;
        *max_descent_out = 0;
        return;
    }
    int const ascent = fallback_lh;
    int max_ascent = ascent - min_oy;
    int max_descent = max_bottom - ascent;
    if( max_ascent <= 0 )
        max_ascent = fallback_lh;
    if( max_descent < 0 )
        max_descent = 0;
    *max_ascent_out = max_ascent;
    *max_descent_out = max_descent;
}

static bool
gl3_font_should_auto_wrap(
    int widget_height,
    int line_height,
    int max_ascent,
    int max_descent)
{
    int const resolved_lh = line_height > 0 ? line_height : 1;
    int const ascent = max_ascent > 0 ? max_ascent : 0;
    int const descent = max_descent > 0 ? max_descent : 0;
    int const height = widget_height > 0 ? widget_height : 0;
    return !(height < resolved_lh + ascent + descent && height < resolved_lh * 2);
}

static bool
gl3_font_append_line(
    char const* lines[],
    int line_lens[],
    int* line_count,
    int max_lines,
    char const* start,
    int len)
{
    if( *line_count >= max_lines )
        return false;
    lines[*line_count] = start;
    line_lens[*line_count] = len;
    (*line_count)++;
    return true;
}

static bool
gl3_font_wrap_segment(
    struct ToriDraw_Font* font,
    char const* text,
    int len,
    int max_width,
    char const* lines[],
    int line_lens[],
    int* line_count,
    int max_lines)
{
    int const space_adv = gl3_font_measure_substring(font, " ", 1);
    int cur_start = -1;
    int cur_len = 0;
    int cur_w = 0;
    int word_start = 0;

    if( len <= 0 )
        return gl3_font_append_line(lines, line_lens, line_count, max_lines, text, 0);

    for( int i = 0; i <= len; i++ )
    {
        bool const at_end = i == len;
        bool const is_space = !at_end && gl3_font_is_space((unsigned char)text[i]);
        if( !at_end && !is_space )
            continue;

        int const word_len = i - word_start;
        if( word_len <= 0 )
        {
            word_start = at_end ? i : i + 1;
            continue;
        }

        int const word_w = gl3_font_measure_substring(font, text + word_start, word_len);
        if( cur_len <= 0 )
        {
            cur_start = word_start;
            cur_len = word_len;
            cur_w = word_w;
        }
        else
        {
            int const candidate = cur_w + space_adv + word_w;
            if( candidate > max_width )
            {
                if( !gl3_font_append_line(
                        lines, line_lens, line_count, max_lines, text + cur_start, cur_len) )
                    return false;
                cur_start = word_start;
                cur_len = word_len;
                cur_w = word_w;
            }
            else
            {
                cur_len = i - cur_start;
                cur_w = candidate;
            }
        }
        word_start = at_end ? i : i + 1;
    }
    if( cur_len > 0 )
        return gl3_font_append_line(
            lines, line_lens, line_count, max_lines, text + cur_start, cur_len);
    return true;
}

static int
gl3_font_collect_lines(
    struct ToriDraw_Font* font,
    char const* text,
    int w,
    int h,
    int line_height,
    char const* lines[],
    int line_lens[])
{
    int line_count = 0;
    if( !text || text[0] == '\0' )
        return 0;

    int const resolved_lh =
        line_height > 0 ? line_height : (font->line_height > 0 ? font->line_height : 1);
    int const logical_w = w > 0 ? w : 1;
    int max_ascent = resolved_lh;
    int max_descent = 0;
    gl3_font_vertical_metrics(font, &max_ascent, &max_descent);
    bool const auto_wrap =
        w > 0 && h > 0 && gl3_font_should_auto_wrap(h, resolved_lh, max_ascent, max_descent);

    char const* rest = text;
    while( rest && rest[0] != '\0' && line_count < GL3_FONT_BOX_MAX_LINES )
    {
        int segment_len = 0;
        int break_advance = 0;
        char const* break_at = gl3_font_next_line(rest, &segment_len, &break_advance);
        if( auto_wrap )
        {
            if( !gl3_font_wrap_segment(
                    font,
                    rest,
                    segment_len,
                    logical_w,
                    lines,
                    line_lens,
                    &line_count,
                    GL3_FONT_BOX_MAX_LINES) )
                break;
        }
        else
        {
            if( !gl3_font_append_line(
                    lines, line_lens, &line_count, GL3_FONT_BOX_MAX_LINES, rest, segment_len) )
                break;
        }
        if( break_advance == 0 )
            break;
        rest = break_at + break_advance;
    }
    return line_count;
}

struct GL3FontGlyphCtx
{
    struct ToriRS_GL3* renderer;
    struct GL3FontSlot* slot;
    float alpha;
    bool shadow;
    int quad_count;
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
    struct ToriRS_GL3* renderer = gctx->renderer;
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
    gctx->quad_count++;
}

static void
gl3_draw_font_glyphs(
    struct ToriRS_GL3* renderer,
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
        .quad_count = 0,
    };
    ToriDraw_FontVisitGlyphsStyled(
        font, text, x, y, default_color_rgb, center, gl3_font_glyph_callback, &ctx);

    if( gl3_text_debug_enabled() )
    {
        fprintf(
            stderr,
            "gl3_draw_font_glyphs: x=%d y=%d shadow=%d center=%d quads=%d text=\"%.40s%s\"\n",
            x,
            y,
            (int)shadow,
            (int)center,
            ctx.quad_count,
            text,
            strlen(text) > 40 ? "..." : "");
    }

    gl3_flush_2d_batch(renderer);
    gl3_batch2d_reset(renderer);
}

static void
gl3_draw_font_glyphs_len(
    struct ToriRS_GL3* renderer,
    struct GL3FontSlot* slot,
    struct ToriDraw_Font* font,
    char const* text,
    int len,
    int x,
    int y,
    int default_color_rgb,
    float alpha,
    bool shadow)
{
    char tmp[4096];
    if( len <= 0 )
        return;
    if( len >= (int)sizeof(tmp) )
        len = (int)sizeof(tmp) - 1;
    memcpy(tmp, text, (size_t)len);
    tmp[len] = '\0';
    gl3_draw_font_glyphs(renderer, slot, font, tmp, x, y, default_color_rgb, alpha, shadow, false);
}

static void
gl3_draw_font_box(
    struct ToriRS_GL3* renderer,
    struct GL3FontSlot* slot,
    struct ToriDraw_Font* font,
    struct ToriRS_RenderCommand_Font const* cmd)
{
    char const* lines[GL3_FONT_BOX_MAX_LINES];
    int line_lens[GL3_FONT_BOX_MAX_LINES];
    int const line_count =
        gl3_font_collect_lines(font, cmd->text, cmd->w, cmd->h, cmd->line_height, lines, line_lens);
    if( line_count <= 0 )
        return;

    int const resolved_lh = cmd->line_height > 0 ? cmd->line_height
                                                 : (font->line_height > 0 ? font->line_height : 1);
    int const logical_w = cmd->w > 0 ? cmd->w : 1;
    int const font_ascent = font->line_height > 0 ? font->line_height : resolved_lh;
    int max_ascent = resolved_lh;
    int max_descent = 0;
    gl3_font_vertical_metrics(font, &max_ascent, &max_descent);
    int const block_h = line_count > 0 ? resolved_lh * (line_count - 1) + max_ascent + max_descent
                                       : max_ascent + max_descent;
    int const logical_h = cmd->h > 0 ? cmd->h : block_h;

    int base_y0 = max_ascent;
    if( cmd->y_align == 1 )
    {
        int const space = logical_h - max_ascent - max_descent - resolved_lh * (line_count - 1);
        base_y0 = max_ascent + space / 2;
    }
    else if( cmd->y_align == 2 )
        base_y0 = logical_h - max_descent - resolved_lh * (line_count - 1);

    for( int i = 0; i < line_count; i++ )
    {
        int line_x = cmd->x;
        if( line_lens[i] > 0 )
        {
            int const tw = gl3_font_measure_substring(font, lines[i], line_lens[i]);
            if( cmd->center == 1 )
                line_x = cmd->x + (logical_w - tw) / 2;
            else if( cmd->center == 2 )
                line_x = cmd->x + logical_w - tw;
        }
        int const draw_y = cmd->y + base_y0 + i * resolved_lh - font_ascent;
        if( line_lens[i] <= 0 )
            continue;
        if( cmd->shadowed )
            gl3_draw_font_glyphs_len(
                renderer,
                slot,
                font,
                lines[i],
                line_lens[i],
                line_x + 1,
                draw_y + 1,
                cmd->color,
                1.0f,
                true);
        gl3_draw_font_glyphs_len(
            renderer,
            slot,
            font,
            lines[i],
            line_lens[i],
            line_x,
            draw_y,
            cmd->color,
            1.0f,
            false);
    }
}

static void
gl3_ev_fill_rect(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
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

    if( command->u.fill_rect.filled )
    {
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
        return;
    }

    /* Outline: Soft3D uses ToriDraw2D_DrawRectOutline (1px edges). Match that
     * with four thin quads so unfilled TYPE_RECT from cc_create stays a border
     * instead of painting an opaque black box over a translucent fill. */
    gl3_draw_textured_quad(
        renderer, renderer->white_texture, 0, false, NULL, (float)x, (float)y,
        (float)(x + w), (float)(y + 1), 0.0f, 0.0f, 1.0f, 1.0f, rgba);
    if( h > 1 )
        gl3_draw_textured_quad(
            renderer, renderer->white_texture, 0, false, NULL, (float)x, (float)(y + h - 1),
            (float)(x + w), (float)(y + h), 0.0f, 0.0f, 1.0f, 1.0f, rgba);
    if( h > 2 )
    {
        gl3_draw_textured_quad(
            renderer, renderer->white_texture, 0, false, NULL, (float)x, (float)(y + 1),
            (float)(x + 1), (float)(y + h - 1), 0.0f, 0.0f, 1.0f, 1.0f, rgba);
        if( w > 1 )
            gl3_draw_textured_quad(
                renderer, renderer->white_texture, 0, false, NULL, (float)(x + w - 1),
                (float)(y + 1), (float)(x + w), (float)(y + h - 1), 0.0f, 0.0f, 1.0f, 1.0f,
                rgba);
    }
}

static void
gl3_ev_begin_2d(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
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
    gl3_bind_quad_attribs(renderer);
    gl3_batch2d_reset(renderer);
    renderer->draw_scissor_x = 0;
    renderer->draw_scissor_y = 0;
    renderer->draw_scissor_w = 0;
    renderer->draw_scissor_h = 0;
}

static void
gl3_ev_end_2d(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    (void)command;
    gl3_flush_2d_batch(renderer);
    gl3_unbind_attribs(renderer);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    renderer->in2d = false;
}

static void
gl3_draw_sprite_tiled(
    struct ToriRS_GL3* renderer,
    struct ToriDraw_Sprite* sp,
    struct ToriRS_RenderCommand* command,
    int tile_sw,
    int tile_sh,
    float u0,
    float v0,
    float u1,
    float v1,
    float const rgba[4])
{
    /* Prefer the variant's pixel size so tile cells match the UV rect; fall
     * back to the sprite's nominal size when the caller has no variant dims. */
    int const tile_w = tile_sw > 0 ? tile_sw : (sp->width > 0 ? sp->width : 1);
    int const tile_h = tile_sh > 0 ? tile_sh : (sp->height > 0 ? sp->height : 1);
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
    trspk_sprite_tile_phase_origin(
        dest_x, dest_y, origin_x, origin_y, tile_w, tile_h, &start_x, &start_y);

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
gl3_ev_sprite_load(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    (void)renderer;
    (void)command;
}

static void
gl3_ev_sprite_unload(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    int scene_id = command->u.sprite_load.element_id;
    int slot_i = gl3_sprite_slot_index(renderer, scene_id, false);
    if( slot_i < 0 )
        return;
    free(renderer->sprite_slots[slot_i].uvs);
    free(renderer->sprite_slots[slot_i].loaded);
    renderer->sprite_slots[slot_i].uvs = NULL;
    renderer->sprite_slots[slot_i].loaded = NULL;
    renderer->sprite_slots[slot_i].count = 0;
    renderer->sprite_slots[slot_i].scene_id = 0;
}

static void
gl3_ev_sprite(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    struct ToriRS_RenderCommand_Sprite const* spr_cmd = &command->u.sprite;
    struct ToriDraw_Sprite* sp = NULL;
    float u0;
    float v0;
    float u1;
    float v1;
    int ox;
    int oy;
    int sw;
    int sh;
    float alpha;
    float rgba[4];
    if( !renderer->in2d )
        return;
    if( spr_cmd->scene_id <= 0 )
        return;
    /* TORIRS_GL_SPRITE_DEBUG=1: every sprite the frame draws, with the flags
     * that pick its path. Which path a given widget takes is not obvious from
     * the command, and it is the first thing to establish when one backend
     * draws it and another does not. */
    {
        static int dbg = -1;
        static int printed = 0;
        if( dbg < 0 )
            dbg = getenv("TORIRS_GL_SPRITE_DEBUG") != NULL;
        if( dbg && printed < 40 )
        {
            printed++;
            fprintf(
                stderr,
                "SPRITE scene=%d idx=%d at=%d,%d wh=%dx%d rot=%d mask=%d tiled=%d if3=%d\n",
                spr_cmd->scene_id, spr_cmd->atlas_index, spr_cmd->x, spr_cmd->y,
                spr_cmd->w, spr_cmd->h, spr_cmd->rotated ? spr_cmd->rotation_r2pi2048 : -1,
                spr_cmd->mask_scene_id, spr_cmd->tiled ? 1 : 0, spr_cmd->if3 ? 1 : 0);
        }
    }
    gl3_set_draw_scissor(renderer, spr_cmd->scissor_x, spr_cmd->scissor_y, spr_cmd->scissor_w, spr_cmd->scissor_h);
    alpha = (float)(255 - spr_cmd->trans) / 255.0f;
    if( alpha < 0.0f ) alpha = 0.0f;
    if( alpha > 1.0f ) alpha = 1.0f;
    rgba[0] = rgba[1] = rgba[2] = 1.0f;
    rgba[3] = alpha;
    if( spr_cmd->rotated && spr_cmd->mask_scene_id > 0 )
    {
        int dst_w;
        int dst_h;
        float mask_uv[4];
        GLuint rotmask_tex = 0u;
        int spr_count = 0;
        struct ToriDraw_Sprite** sprites;
        /* Do not ensure_base/atlas-upload the source: the minimap pixmap is
         * enormous and must not compete for the shared 2D atlas. Soft3D only
         * needs the live pixel pointer for the CPU blit; so do we. */
        sprites = ToriDraw_SceneSpriteGet(renderer->scene, spr_cmd->scene_id, &spr_count);
        if( !sprites || spr_cmd->atlas_index < 0 || spr_cmd->atlas_index >= spr_count )
            return;
        sp = sprites[spr_cmd->atlas_index];
        if( !sp || !sp->pixels_argb )
            return;
        dst_w = spr_cmd->w > 0 ? spr_cmd->w : sp->width;
        dst_h = spr_cmd->h > 0 ? spr_cmd->h : sp->height;
        if( !gl3_sprite_ensure_rotated_masked(renderer, spr_cmd, sp, mask_uv, &rotmask_tex) )
            return;
        gl3_flush_2d_batch(renderer);
        gl3_draw_textured_quad(
            renderer,
            rotmask_tex,
            0,
            true,
            mask_uv,
            (float)spr_cmd->x,
            (float)spr_cmd->y,
            (float)(spr_cmd->x + dst_w),
            (float)(spr_cmd->y + dst_h),
            mask_uv[0],
            mask_uv[1],
            mask_uv[2],
            mask_uv[3],
            rgba);
        /* Commit before the next rotmask rewrite of another dedicated slot. */
        gl3_flush_2d_batch(renderer);
        return;
    }
    {
        float uvtmp[4];
        if( !gl3_sprite_ensure_variant(renderer, spr_cmd, &sp, uvtmp, &ox, &oy, &sw, &sh) )
            return;
        u0 = uvtmp[0]; v0 = uvtmp[1]; u1 = uvtmp[2]; v1 = uvtmp[3];
    }
    if( spr_cmd->rotated )
    {
        gl3_draw_sprite_rotated(renderer, command, sp, u0, v0, u1, v1, rgba);
        return;
    }
    if( spr_cmd->tiled )
    {
        gl3_draw_sprite_tiled(renderer, sp, command, sw, sh, u0, v0, u1, v1, rgba);
        return;
    }
    if( spr_cmd->if3 )
    {
        /* Soft3D scales the *nominal* box (spr->width x spr->height) with the
         * image pasted at (ox, oy) inside it, then scales that box to
         * cmd->w x cmd->h. Map the image's sub-rect through the same scale. */
        int const nominal_w = sp->width > 0 ? sp->width : (sw > 0 ? sw : 1);
        int const nominal_h = sp->height > 0 ? sp->height : (sh > 0 ? sh : 1);
        int const box_w = spr_cmd->w > 0 ? spr_cmd->w : nominal_w;
        int const box_h = spr_cmd->h > 0 ? spr_cmd->h : nominal_h;
        float const sx = (float)box_w / (float)nominal_w;
        float const sy = (float)box_h / (float)nominal_h;
        /* A rotated variant is larger than the nominal box it was baked from
         * (sqrt(2) at 45 degrees), and it has to stay centred on the box —
         * Soft3D does the same. Unrotated this is the identity, since then
         * sw x sh is exactly the nominal box. */
        float const rot_cx =
            spr_cmd->sprite_angle_r2pi65536 != 0 ? ((float)box_w - (float)sw * sx) * 0.5f : 0.0f;
        float const rot_cy =
            spr_cmd->sprite_angle_r2pi65536 != 0 ? ((float)box_h - (float)sh * sy) * 0.5f : 0.0f;
        float const dx0 = (float)spr_cmd->x + (float)ox * sx + rot_cx;
        float const dy0 = (float)spr_cmd->y + (float)oy * sy + rot_cy;
        float const dx1 = dx0 + (float)sw * sx;
        float const dy1 = dy0 + (float)sh * sy;
        float const uv_bounds[4] = { u0, v0, u1, v1 };
        int clip_x = 0;
        int clip_y = 0;
        int clip_w = 0;
        int clip_h = 0;
        trspk_rect_intersect(
            spr_cmd->scissor_x,
            spr_cmd->scissor_y,
            spr_cmd->scissor_w,
            spr_cmd->scissor_h,
            spr_cmd->x,
            spr_cmd->y,
            box_w,
            box_h,
            &clip_x,
            &clip_y,
            &clip_w,
            &clip_h);
        gl3_set_draw_scissor(renderer, clip_x, clip_y, clip_w, clip_h);
        gl3_draw_textured_quad(
            renderer,
            renderer->sprite_atlas_texture,
            0,
            true,
            uv_bounds,
            dx0,
            dy0,
            dx1,
            dy1,
            u0,
            v0,
            u1,
            v1,
            rgba);
        return;
    }
    {
        /* Non-if3 sprites always blit at natural pixel size; cmd->w/h is the
         * widget box and must not stretch the icon. */
        int draw_x = spr_cmd->x + ox;
        int draw_y = spr_cmd->y + oy;
        int draw_w = sw;
        int draw_h = sh;
        if( spr_cmd->sprite_angle_r2pi65536 != 0 )
        {
            int center_x = spr_cmd->x + ox + sw / 2;
            int center_y = spr_cmd->y + oy + sh / 2;
            draw_x = center_x - sw / 2;
            draw_y = center_y - sh / 2;
        }
        float const uv_bounds[4] = { u0, v0, u1, v1 };
        gl3_draw_textured_quad(renderer, renderer->sprite_atlas_texture, 0, true, uv_bounds,
            (float)draw_x, (float)draw_y, (float)(draw_x + draw_w), (float)(draw_y + draw_h),
            u0, v0, u1, v1, rgba);
    }
}

static void
gl3_ev_font_load(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    const int font_id = command->u.font_load.font_id;
    int slot_i = gl3_font_slot_index(renderer, font_id, true);
    if( slot_i < 0 )
        return;
    struct GL3FontSlot* slot = &renderer->font_slots[slot_i];
    if( slot->font != command->u.font_load.font )
    {
        slot->font = command->u.font_load.font;
        slot->baked = false;
    }
    struct GL3FontSlot* baked_slot = gl3_ensure_font_slot(renderer, font_id);
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
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
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
    struct GL3FontSlot* slot = gl3_ensure_font_slot(renderer, font_id);
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

    if( gl3_text_debug_enabled() )
    {
        int line_count = 0;
        if( !command->u.font.baseline )
        {
            char const* lines[GL3_FONT_BOX_MAX_LINES];
            int line_lens[GL3_FONT_BOX_MAX_LINES];
            line_count = gl3_font_collect_lines(
                font,
                command->u.font.text,
                command->u.font.w,
                command->u.font.h,
                command->u.font.line_height,
                lines,
                line_lens);
        }
        fprintf(
            stderr,
            "gl3_ev_font: font_id=%d baseline=%d xy=%d,%d wh=%dx%d center=%d y_align=%d "
            "lh=%d scissor=%d,%d %dx%d lines=%d text=\"%.40s%s\"\n",
            font_id,
            command->u.font.baseline,
            command->u.font.x,
            command->u.font.y,
            command->u.font.w,
            command->u.font.h,
            command->u.font.center,
            command->u.font.y_align,
            command->u.font.line_height,
            command->u.font.scissor_x,
            command->u.font.scissor_y,
            command->u.font.scissor_w,
            command->u.font.scissor_h,
            line_count,
            command->u.font.text,
            strlen(command->u.font.text) > 40 ? "..." : "");
    }

    if( !command->u.font.baseline )
    {
        gl3_draw_font_box(renderer, slot, font, &command->u.font);
        return;
    }

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


static void
upload_world_ubo(
    struct ToriRS_GL3* renderer,
    const float* view,
    const float* proj)
{
    TRSPK_UboWorld u = { 0 };
    memcpy(u.modelViewMatrix, view, sizeof(float) * 16u);
    memcpy(u.projectionMatrix, proj, sizeof(float) * 16u);
    u.uClock = (float)renderer->frame_clock;
    u.uAtlasDim = (float)renderer->atlas_dim;
    u.uAtlasSlots = (float)renderer->tex_cap;
    gl3_set_world_uniforms(renderer, &u);
}

/*
 * Push only the rectangle that changed.
 *
 * This used to be a glTexImage2D of the whole atlas on every dirty flag — 16 MB
 * for a 2048^2 RGBA atlas, and glTexImage2D *reallocates* the texture rather
 * than writing into it, so the driver also threw away and revalidated the
 * object each time. One newly-decoded 128^2 tile cost the entire atlas.
 *
 * D3D9 has answered this correctly since it was written (see
 * WINDOWS-D3D9-UPLOAD-001: "a single changed 128x128 tile copies 65,536 logical
 * bytes, not the complete 2048x2048 atlas"). This is the same discipline: the
 * CPU atlas merges an exact dirty rectangle, and the upload covers that.
 *
 * The rows are packed into a scratch buffer first because WebGL1 has no
 * GL_UNPACK_ROW_LENGTH — that is a GLES3/desktop pixel-store parameter, so a
 * sub-rectangle of a wider source cannot be handed to glTexSubImage2D directly.
 * Desktop GL could set it and skip the pack; it deliberately does not, so both
 * backends run one path and a bug in it cannot hide on the host nobody tests.
 * The pack is a memcpy per row (the atlas is already RGBA — unlike D3D9, which
 * has to swizzle to BGRA as it copies).
 */
static void
gl3_upload_atlas_texture(struct ToriRS_GL3* renderer)
{
    struct TRSPK_AtlasDirtyRect dirty;
    size_t need;

    if( !trspk_atlas_is_initialized(&renderer->atlas) || !renderer->atlas_texture )
        return;
    if( !renderer->atlas.pixels || renderer->atlas.width == 0u || renderer->atlas.height == 0u )
        return;
    if( !trspk_atlas_get_dirty_rect(&renderer->atlas, &dirty) || dirty.w == 0u || dirty.h == 0u )
        return;

    glBindTexture(GL_TEXTURE_2D, renderer->atlas_texture);

    if( !renderer->atlas_storage_ready )
    {
        /* Allocate the texture once. TORIRS_GL_TEX_RGBA is a sized internal
         * format on Core Profile / Metal-backed GL and plain GL_RGBA on GLES2,
         * where internalformat must equal format. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            TORIRS_GL_TEX_RGBA,
            (GLsizei)renderer->atlas.width,
            (GLsizei)renderer->atlas.height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            renderer->atlas.pixels);
        renderer->atlas_storage_ready = true;
        TORIRS_PERF_COUNT(
            TORIRS_PERF_CTR_GL_ATLAS_UPLOAD_BYTES,
            (int64_t)((uint64_t)renderer->atlas.width * renderer->atlas.height * 4u));
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATLAS_UPLOADS, 1);
        trspk_atlas_clear_dirty(&renderer->atlas);
        return;
    }

    need = (size_t)dirty.w * (size_t)dirty.h * 4u;
    if( need > renderer->atlas_stage_capacity )
    {
        /* Never shrunk: the worst rectangle a scene produces is the size that
         * matters, and re-allocating per upload would churn the wasm heap. */
        size_t cap = renderer->atlas_stage_capacity ? renderer->atlas_stage_capacity : 65536u;
        uint8_t* grown;
        while( cap < need )
            cap *= 2u;
        grown = (uint8_t*)realloc(renderer->atlas_stage, cap);
        if( !grown )
            return;
        renderer->atlas_stage = grown;
        renderer->atlas_stage_capacity = cap;
    }

    for( uint32_t y = 0u; y < dirty.h; y++ )
    {
        const uint8_t* src = renderer->atlas.pixels +
                             (size_t)(dirty.y + y) * renderer->atlas.stride +
                             (size_t)dirty.x * 4u;
        memcpy(renderer->atlas_stage + (size_t)y * dirty.w * 4u, src, (size_t)dirty.w * 4u);
    }

    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        (GLint)dirty.x,
        (GLint)dirty.y,
        (GLsizei)dirty.w,
        (GLsizei)dirty.h,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        renderer->atlas_stage);

    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATLAS_UPLOAD_BYTES, (int64_t)need);
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATLAS_UPLOADS, 1);
    trspk_atlas_clear_dirty(&renderer->atlas);
}

static void
gl3_bind_world_draw_state(struct ToriRS_GL3* renderer)
{
    glUseProgram(renderer->program3d);
    gl3_push_world_uniforms(renderer);
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
gl3_release_gpu_mesh_buffers(struct ToriRS_GL3* renderer)
{
    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        struct GL3ModelGroup* g = &renderer->groups[gi];
        if( g->vbo_gpu )
        {
            glDeleteBuffers(1, &g->vbo_gpu);
            glGenBuffers(1, &g->vbo_gpu);
#if !defined(TORIRS_GL_ES2)
            gl3_bind_group_attribs(renderer, g, 0u);
#endif
            glBindBuffer(GL_ARRAY_BUFFER, g->vbo_gpu);
            glBufferData(
                GL_ARRAY_BUFFER,
                TRSPK_GL3_GPU_VBO_INIT * sizeof(struct TRSPK_VertexOpenGl3),
                NULL,
                GL_DYNAMIC_DRAW);
            bind_vbo_attribs(renderer, g->vbo_gpu, 0u);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
            gl3_unbind_attribs(renderer);
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
            TRSPK_GL3_GPU_IBO_INIT * TORIRS_GL_INDEX_SIZE,
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

    /* Counted per group so a steady scene can be asserted to move no static
     * vertices — the dynamic group is reset and re-uploaded every frame by
     * design, the static one must not be. */
    if( g->reset_each_frame )
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DYNAMIC_VBO_UPLOAD_BYTES, (int64_t)byte_size);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DYNAMIC_VBO_UPLOADS, 1);
    }
    else
    {
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_STATIC_VBO_UPLOAD_BYTES, (int64_t)byte_size);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_STATIC_VBO_UPLOADS, 1);
    }
    trspk_vbo_clear_dirty(g->vbo_cpu);
    return true;
}

#if defined(TORIRS_GL_ES2)
/* Staging for the 16-bit index stream and the chunk table it is split into.
 * Both grow with the frame and are never shrunk: a scene's worst frame is the
 * size that matters, and re-allocating per frame would churn the wasm heap. */
static bool
gl3_ensure_index16(
    struct ToriRS_GL3* renderer,
    uint32_t index_count)
{
    if( index_count > renderer->idx16_capacity )
    {
        uint32_t cap = renderer->idx16_capacity ? renderer->idx16_capacity : 4096u;
        uint16_t* grown;
        while( cap < index_count )
            cap *= 2u;
        grown = (uint16_t*)realloc(renderer->idx16, (size_t)cap * sizeof(uint16_t));
        if( !grown )
            return false;
        renderer->idx16 = grown;
        renderer->idx16_capacity = cap;
    }
    {
        /* Worst case is one chunk per triangle; in practice a chunk covers a
         * whole range, so track the range count and grow if a frame proves
         * otherwise. */
        const uint32_t want = (index_count / 3u) + 64u;
        if( want > renderer->chunk_capacity )
        {
            uint32_t cap = renderer->chunk_capacity ? renderer->chunk_capacity : 1024u;
            struct TRSPK_WebGL1Chunk* grown;
            while( cap < want )
                cap *= 2u;
            grown = (struct TRSPK_WebGL1Chunk*)realloc(
                renderer->chunks, (size_t)cap * sizeof(struct TRSPK_WebGL1Chunk));
            if( !grown )
                return false;
            renderer->chunks = grown;
            renderer->chunk_capacity = cap;
        }
    }
    return true;
}
#endif

static bool
gl3_ensure_gpu_ibo(
    struct ToriRS_GL3* renderer,
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
        GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(cap * TORIRS_GL_INDEX_SIZE), NULL, GL_DYNAMIC_DRAW);
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


static void
gl3_decode_texture_rgba(
    struct ToriDraw_Texture const* tex,
    uint32_t tile_size,
    uint8_t* out_rgba)
{
    uint32_t const src_w = (uint32_t)tex->width;
    uint32_t const src_h = (uint32_t)tex->height;
    uint32_t const pixel_count = tile_size * tile_size * 4u;

    memset(out_rgba, 0, pixel_count);
    if( src_w == 0u || src_h == 0u )
        return;

    for( uint32_t dst_row = 0; dst_row < tile_size; dst_row++ )
    {
        uint32_t const src_row = (dst_row * src_h) / tile_size;
        for( uint32_t dst_col = 0; dst_col < tile_size; dst_col++ )
        {
            uint32_t const src_col = (dst_col * src_w) / tile_size;
            int const texel = tex->texels[src_row * src_w + src_col];
            uint8_t const rv = (uint8_t)((texel >> 16) & 0xFF);
            uint8_t const gv = (uint8_t)((texel >> 8) & 0xFF);
            uint8_t const bv = (uint8_t)(texel & 0xFF);
            uint8_t const av = (tex->opaque || texel != 0) ? 255u : 0u;
            uint32_t const idx = (dst_row * tile_size + dst_col) * 4u;
            out_rgba[idx + 0] = rv;
            out_rgba[idx + 1] = gv;
            out_rgba[idx + 2] = bv;
            out_rgba[idx + 3] = av;
        }
    }
}

/* -----------------------------------------------------------------------
 * Render-command dispatch
 * ----------------------------------------------------------------------- */

static void
gl3_ev_model_unload(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command);

/** Allocate or look up the atlas slot for a cache texture id. Returns -1 when
 *  the id is out of range or the atlas is full. */
static int
gl3_texture_slot(
    struct ToriRS_GL3* renderer,
    int tex_id)
{
    int slot;

    assert(renderer);
    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return -1;
    slot = renderer->tex_slot_of_id[tex_id];
    if( slot >= 0 )
        return slot;
    if( renderer->tex_slot_next >= renderer->tex_cap )
    {
        static int warned;
        if( !warned )
        {
            fprintf(
                stderr,
                "gl3: texture atlas full (%u slots); further textures dropped\n",
                renderer->tex_cap);
            warned = 1;
        }
        return -1;
    }
    slot = (int)renderer->tex_slot_next++;
    renderer->tex_slot_of_id[tex_id] = slot;
    return slot;
}

static void
gl3_ev_tex_load(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_TEX_LOAD);

    int tex_id = command->u.tex_load.texture_id;
    struct ToriDraw_Texture* tex = command->u.tex_load.texture;
    int slot;

    if( !tex || !tex->texels )
        return;
    slot = gl3_texture_slot(renderer, tex_id);
    if( slot < 0 )
        return;

    static uint8_t rgba_scratch[TRSPK_ATLAS_TILE * TRSPK_ATLAS_TILE * 4u];
    gl3_decode_texture_rgba(tex, TRSPK_ATLAS_TILE, rgba_scratch);

    trspk_atlas_grid_insert_at(
        &renderer->atlas,
        (uint32_t)slot,
        rgba_scratch,
        TRSPK_ATLAS_TILE * 4u,
        TRSPK_ATLAS_TILE,
        TRSPK_ATLAS_TILE,
        NULL);
    renderer->tex_resident[slot] = 1u;
}

static void
gl3_ev_tex_unload(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_TEX_UNLOAD);

    const int tex_id = command->u.tex_load.texture_id;
    int slot;

    if( tex_id < 0 || tex_id >= TORIDRAW_TEXTURE_ID_CAPACITY || !renderer->atlas.pixels )
        return;
    slot = renderer->tex_slot_of_id[tex_id];
    if( slot < 0 || (uint32_t)slot >= renderer->tex_cap )
        return;

    struct TRSPK_AtlasTile tile;
    if( !trspk_atlas_grid_tile_for_slot(&renderer->atlas, (uint32_t)slot, &tile) )
        return;

    const size_t row_bytes = (size_t)tile.w * renderer->atlas.channels;
    for( uint32_t row = 0; row < tile.h; row++ )
    {
        uint8_t* dst = renderer->atlas.pixels + (size_t)(tile.y + row) * renderer->atlas.stride +
                       (size_t)tile.x * renderer->atlas.channels;
        memset(dst, 0, row_bytes);
    }
    trspk_atlas_set_dirty(&renderer->atlas);
    renderer->tex_resident[slot] = 0u;
    /* Keep the slot mapping so a re-load reuses the same tile; the encode
     * already baked the slot into resident VBOs. */
}

static void
gl3_ev_font_unload(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    const int font_id = command->u.font_load.font_id;
    int slot_i = gl3_font_slot_index(renderer, font_id, false);
    if( slot_i < 0 )
        return;

    struct GL3FontSlot* slot = &renderer->font_slots[slot_i];
    if( slot->texture )
    {
        glDeleteTextures(1, &slot->texture);
        slot->texture = 0;
    }
    slot->font = NULL;
    slot->baked = false;
    slot->atlas_w = 0;
    slot->atlas_h = 0;
    slot->font_id = -1;
    memset(slot->glyph_uv, 0, sizeof(slot->glyph_uv));
}

static void
gl3_ev_anim_unload(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_ANIM_UNLOAD);
    gl3_ev_model_unload(renderer, command);
}

static void
gl3_ev_begin_3d(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
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

    {
        const struct ToriDraw_ViewPort* vp = &renderer->cur_3d.view_port;
        const struct ToriDraw_Camera* cam = &renderer->cur_3d.camera;
        const struct ToriDraw_Position* cam_pos = &renderer->cur_3d.camera_position;
        const int pass_w = vp->width > 0 ? vp->width : renderer->width;
        const int pass_h = vp->height > 0 ? vp->height : renderer->height;
        trspk_compute_pass_matrices(
            renderer->view,
            renderer->proj,
            (float)cam_pos->x,
            (float)cam_pos->y,
            (float)cam_pos->z,
            ToriDraw_AngleToRadians(cam->pitch),
            ToriDraw_AngleToRadians(cam->yaw),
            pass_w,
            pass_h,
            /* The camera's own projection, not an assumed one. The GPU used to
             * project at a hardcoded scale of 512 while the rasterizer used
             * whatever the layout computed — at this boot's viewport that is
             * ~191, so the GPU drew the world 2.7x magnified about the viewport
             * centre. Identical camera, identical scene, different size. */
            (int)cam->proj_mode,
            cam->proj_scale,
            cam->fov_rpi2048,
            cam->parallel_zoom16);
    }
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
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    (void)command;

    if( !renderer->groups[TRSPK_VBO_GROUP_STATIC].arena )
        return;

    trspk_pose_table_clear(&renderer->poses);
    gl3_reset_model_group(&renderer->groups[TRSPK_VBO_GROUP_STATIC]);
    gl3_release_gpu_mesh_buffers(renderer);
}

static void
gl3_ev_model_unload(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_MODEL_UNLOAD || command->kind == TORIRSRC_ANIM_UNLOAD);

    const int element_id = command->u.model_load.element_id;
    trspk_modelarena_unload_element(renderer->groups[TRSPK_VBO_GROUP_STATIC].arena, element_id);
    trspk_pose_table_remove_element(&renderer->poses, element_id);
}

/** Ensure `tex_id` is resident in the atlas. Returns the atlas slot, or -1. */
static int
gl3_ensure_texture(
    struct ToriRS_GL3* renderer,
    int tex_id)
{
    struct ToriDraw_Texture* tex;
    static uint8_t rgba_scratch[TRSPK_ATLAS_TILE * TRSPK_ATLAS_TILE * 4u];
    int slot;

    if( tex_id < 0 || !renderer->scene )
        return -1;
    slot = gl3_texture_slot(renderer, tex_id);
    if( slot < 0 )
        return -1;
    if( renderer->tex_resident[slot] )
        return slot;
    tex = ToriDraw_TextureMapGet(
        &ToriDraw_SceneTexState(renderer->scene)->texture_map, tex_id);
    if( !tex || !tex->texels )
        return -1;
    gl3_decode_texture_rgba(tex, TRSPK_ATLAS_TILE, rgba_scratch);
    if( !trspk_atlas_grid_insert_at(
            &renderer->atlas,
            (uint32_t)slot,
            rgba_scratch,
            TRSPK_ATLAS_TILE * 4u,
            TRSPK_ATLAS_TILE,
            TRSPK_ATLAS_TILE,
            NULL) )
        return -1;
    renderer->tex_resident[slot] = 1u;
    return slot;
}

static uint32_t
gl3_bake_into_arena(
    struct ToriRS_GL3* renderer,
    struct GL3ModelGroup* g,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle model_handle,
    const struct ToriDraw_Position* world_position,
    bool update_pose_table)
{
    int face_count_faces;
    assert(g->arena && g->vbo_cpu);
    face_count_faces = trspk_toridraw_face_count(model_handle);
    if( face_count_faces <= 0 )
        return UINT32_MAX;
    struct ToriDraw_Scene* ctx = renderer->scene;
    assert(ctx);
    const uint32_t vert_count = (uint32_t)face_count_faces * 3u;
    const uint32_t tri_count = (uint32_t)face_count_faces;

    int arena_pose_id;
    if( anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT || pose_id < 0 ||
        pose_id > (INT_MAX - anim_index) / TRSPK_POSE_TRACK_COUNT )
        return UINT32_MAX;
    arena_pose_id = pose_id * TRSPK_POSE_TRACK_COUNT + anim_index;

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
        if( !trspk_toridraw_bake_face_handle(
                model_handle, face_index, world_position, ctx, true, &face) )
            continue;
        {
            int const slot = gl3_ensure_texture(renderer, face.tex_id);
            face.tex_id_encoded = trspk_encode_vertex_tex_id(
                slot, face.tex_cutout, (int)renderer->tex_cap);
        }

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
gl3_animation_track_unload(
    struct ToriRS_GL3* renderer,
    int element_id,
    int anim_index)
{
    struct TRSPK_ModelArena* arena;

    if( !renderer || element_id < 0 || anim_index < 0 ||
        anim_index >= TRSPK_POSE_TRACK_COUNT )
        return;
    arena = renderer->groups[TRSPK_VBO_GROUP_STATIC].arena;
    if( !arena )
        return;

    for( uint32_t slot_index = 0u; slot_index < arena->slot_count; slot_index++ )
    {
        const struct TRSPK_ModelSlot* slot = &arena->slots[slot_index];
        if( trspk_modelslot_is_alive(slot) && slot->element_id == element_id &&
            slot->pose_id % TRSPK_POSE_TRACK_COUNT == anim_index )
            trspk_modelarena_unload(arena, slot_index);
    }
    trspk_pose_table_remove_track(&renderer->poses, element_id, anim_index);
}

static void
gl3_ev_anim_load(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_ANIM_LOAD);

    const int element_id = command->u.anim_load.element_id;
    struct ToriDraw_Animation* animation = command->u.anim_load.animation;
    struct ToriDraw_SkeletalAnim* skeletal = animation ? animation->skeletal : NULL;
    const struct ToriDraw_ModelHandle* base_handle = &command->u.anim_load.model;
    const struct ToriDraw_Position* world_position = &command->u.anim_load.world_position;
    int anim_index = command->u.anim_load.anim_index;

    if( !animation || animation->frame_count <= 0 ||
        (!skeletal && (!animation->base || !animation->frames)) ||
        anim_index < 0 || anim_index >= TRSPK_POSE_TRACK_COUNT ||
        base_handle->kind != TORIDRAWMK_MODEL ||
        !base_handle->u.model.model )
        return;

    struct ToriDraw_Model* source = base_handle->u.model.model;

    /* Pose keys do not carry a sequence id.  Clear the old track so frame zero
     * cannot keep resolving to MODEL_LOAD's rest pose and a shorter replacement
     * cannot serve stale tail frames. */
    gl3_animation_track_unload(renderer, element_id, anim_index);

    for( int frame = 0; frame < animation->frame_count; frame++ )
    {
        struct ToriDraw_Model* baked = ToriDraw_ModelCopy(source);
        assert(baked);

        if( source->original_vertices_x && source->original_vertices_y &&
            source->original_vertices_z && baked->vertex_count == source->vertex_count )
        {
            size_t vertex_bytes =
                (size_t)baked->vertex_count * sizeof(*baked->vertices_x);
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
                ToriDraw_ModelAnimateSkeletal(baked, skeletal, skeletal_frame);
        }
        else if( animation->frames[frame].length > 0 )
            ToriDraw_ModelAnimateFrame(baked, animation->base, &animation->frames[frame]);

        struct ToriDraw_ModelHandle baked_handle = {
            .kind = TORIDRAWMK_MODEL,
            .u.model.model = baked,
        };
        gl3_bake_into_arena(renderer, &renderer->groups[TRSPK_VBO_GROUP_STATIC],
            element_id,
            anim_index,
            frame,
            baked_handle,
            world_position,
            true);

        ToriDraw_ModelFree(baked);
    }
}

static void
gl3_ev_model_load(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    int element_id;
    assert(command->kind == TORIRSRC_MODEL_LOAD);
    element_id = command->u.model_load.element_id;
    trspk_modelarena_unload_element(
        renderer->groups[TRSPK_VBO_GROUP_STATIC].arena, element_id);
    trspk_pose_table_remove_element(&renderer->poses, element_id);
    gl3_bake_into_arena(renderer, &renderer->groups[TRSPK_VBO_GROUP_STATIC],
        element_id,
        0,
        0,
        command->u.model_load.model,
        &command->u.model_load.world_position,
        true);
}

static void
gl3_ev_batch3d_begin(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    (void)renderer;
    (void)command;
}

static void
gl3_ev_batch3d_model_add(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_BATCH3D_MODEL_ADD);
    trspk_modelarena_unload_element(
        renderer->groups[TRSPK_VBO_GROUP_STATIC].arena,
        command->u.batch.element_id);
    trspk_pose_table_remove_element(&renderer->poses, command->u.batch.element_id);
    gl3_bake_into_arena(renderer, &renderer->groups[TRSPK_VBO_GROUP_STATIC],
        command->u.batch.element_id,
        0,
        command->u.batch.pose_id,
        command->u.batch.model,
        &command->u.batch.world_position,
        true);
}

static void
gl3_ev_batch3d_anim_add(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    assert(command->kind == TORIRSRC_BATCH3D_ANIM_ADD);
    gl3_bake_into_arena(renderer, &renderer->groups[TRSPK_VBO_GROUP_STATIC],
        command->u.batch.element_id,
        command->u.batch.anim_index,
        command->u.batch.pose_id,
        command->u.batch.model,
        &command->u.batch.world_position,
        true);
}

static void
gl3_ev_batch3d_end(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    (void)command;

    if( renderer->groups[TRSPK_VBO_GROUP_STATIC].vbo_cpu )
        trspk_vbo_set_dirty(renderer->groups[TRSPK_VBO_GROUP_STATIC].vbo_cpu);
}

static void
gl3_ev_model_draw(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    struct ToriRS_RenderCommand_Model const* mcmd = &command->u.model;
    struct ToriDraw_Position position;
    int anim_index;
    int pose_id;
    uint32_t group = TRSPK_VBO_GROUP_STATIC;
    uint32_t vertex_base = 0u;
    struct ToriDraw_Scene* ctx;
    assert(command->kind == TORIRSRC_DRAW_MODEL);
    if( !renderer->has_3d || !renderer->ibo_chain )
        return;
    if( mcmd->model.kind == TORIDRAWMK_NONE )
        return;
    ctx = renderer->scene;
    assert(ctx);
    if( mcmd->animation && mcmd->element_id >= 0 )
        ToriDraw_SceneElementApplyAnimation(
            ctx, mcmd->element_id, mcmd->anim_index == 0, mcmd->anim_frame);
    position = mcmd->position;
    if( ToriDraw_RenderModel1Project(
            mcmd->model, ctx, &position, &renderer->cur_3d.view_port, &renderer->cur_3d.camera) !=
        TORIDRAW_CULL_VISIBLE )
        return;
    /* Hittest before the face sort: the scene scratch holds this model's
     * projection only until the next model projects, and a model whose faces
     * all sort away must still pick. */
    if( renderer->pick_enabled && mcmd->pickable && mcmd->element_id >= 0 &&
        (mcmd->pick_aabb
             ? ToriDraw_ProjectedModelContainsAabb(ctx, renderer->pick_mouse_x, renderer->pick_mouse_y)
         : mcmd->pick_terrain
             ? ToriDraw_ProjectedTileMouseHitTest(
                   ctx, mcmd->model, &renderer->cur_3d.view_port, renderer->pick_mouse_x,
                   renderer->pick_mouse_y)
             : ToriDraw_ProjectedModelMouseHitTest(
                   ctx, mcmd->model, &renderer->cur_3d.view_port, renderer->pick_mouse_x,
                   renderer->pick_mouse_y)) )
        ToriRS_PickHitsAdd(
            &renderer->pick_hits,
            mcmd->element_id,
            mcmd->pick_terrain,
            mcmd->pick_tile_x,
            mcmd->pick_tile_z,
            mcmd->pick_tile_level);
    if( mcmd->pick_only )
        return;
    {
        int face_count = ToriDraw_RenderModel2SortFaces(mcmd->model, ctx);
        int* face_order;
        if( face_count <= 0 )
            return;
        anim_index = mcmd->anim_index;
        if( anim_index < 0 )
            anim_index = 0;
        else if( anim_index >= TRSPK_POSE_TRACK_COUNT )
            anim_index = TRSPK_POSE_TRACK_COUNT - 1;
        pose_id = mcmd->animation && mcmd->anim_frame >= 0
                      ? mcmd->anim_frame
                      : 0;
        if( mcmd->animation && mcmd->animation->frame_count > 0 &&
            pose_id >= mcmd->animation->frame_count )
            pose_id = 0;
        if( mcmd->dynamic )
        {
            vertex_base = gl3_bake_into_arena(renderer, &renderer->groups[TRSPK_VBO_GROUP_DYNAMIC],
                mcmd->element_id, anim_index, pose_id, mcmd->model, &mcmd->world_position, false);
            if( vertex_base == UINT32_MAX )
                return;
            group = TRSPK_VBO_GROUP_DYNAMIC;
        }
        else
        {
            if( !trspk_pose_table_get(
                    &renderer->poses, mcmd->element_id, anim_index, pose_id, &vertex_base) )
            {
                gl3_bake_into_arena(renderer, &renderer->groups[TRSPK_VBO_GROUP_STATIC],
                    mcmd->element_id, anim_index, pose_id, mcmd->model, &mcmd->world_position, true);
                if( !trspk_pose_table_get(
                        &renderer->poses, mcmd->element_id, anim_index, pose_id, &vertex_base) )
                    return;
            }
        }
        face_order = ToriDraw_FaceOrder(ctx);
        for( int i = 0; i < face_count; i++ )
        {
            uint32_t face = (uint32_t)face_order[i];
            uint32_t b = vertex_base + face * 3u;
            uint32_t idx[3] = { b, b + 1u, b + 2u };
            trspk_ibochain_push32(renderer->ibo_chain, group, 0u, idx, 3u);
        }
    }
}

static void
gl3_ev_end_3d(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    (void)command;
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

    {
        /* Painter-order fragmentation: how many separate ranges the frame's
         * draw order produced, against the draw calls they turn into. On
         * WebGL1 the second can exceed the first, because a range crossing a
         * 65536-vertex window has to be split. */
        uint32_t range_count = 0u;
        for( const struct TRSPK_DrawRange* r = trspk_drawrangelist_head(renderer->draw_ranges);
             r != NULL;
             r = trspk_drawrangelist_next(renderer->draw_ranges, r) )
            range_count++;
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_RANGES, (int64_t)range_count);
    }

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

#if defined(TORIRS_GL_ES2)
    /*
     * WebGL1 indexes with 16 bits and nothing else, so the absolute indices
     * built above are re-expressed as (base vertex, local index) chunks and the
     * base is folded into the attribute pointers — see webgl1_index16.h. The
     * index buffer is uploaded once per frame, as on GL3; only the width and
     * the per-chunk rebind differ.
     */
    if( !gl3_ensure_index16(renderer, total_indices) )
        goto done;
    {
        uint32_t chunk_count = 0u;
        int overflow = 0;
        uint32_t straddle = 0u;
        /*
         * The searching split, not the paged one.
         *
         * Paging the arena guarantees a triangle never straddles a 16-bit
         * window, which sounds like the stronger property — but it fixes the
         * window to page boundaries, and a page boundary is an arbitrary place
         * to cut. The sliding window merges any run of triangles whose vertex
         * span happens to fit, wherever it starts, so it is strictly more
         * permissive. Measured on the same settled scene: 2,878 chunks
         * searching versus 2,990 paged. Paging was the worse of the two and is
         * kept only for the invariant check it makes possible.
         */
        const uint32_t written = trspk_webgl1_split16(
            renderer->draw_ranges,
            staging,
            renderer->idx16,
            renderer->chunks,
            renderer->chunk_capacity,
            &chunk_count,
            &overflow);

        if( overflow )
            fprintf(
                stderr,
                "WebGL1: draw-chunk table full at %u; dropping the rest of the frame\n",
                renderer->chunk_capacity);
        /*
         * TORIRS_GL_CHUNK_DEBUG=1: one frame's chunking, so "the split is not
         * grouping" and "the split is grouping and painter order defeats it"
         * can be told apart with numbers instead of argued about.
         */
        /*
         * TORIRS_GL_CHUNK_DEBUG=1: one frame's chunking, and — the part that
         * actually explains it — how far apart in the arena consecutive drawn
         * triangles are. A chunk closes when that distance exceeds a 64K
         * window, so the delta histogram is the cause and the chunk count is
         * only the symptom.
         */
        if( getenv("TORIRS_GL_CHUNK_DEBUG") )
        {
            static int dumped = 0;
            if( !dumped && chunk_count > 0u )
            {
                uint32_t singleton = 0u;
                uint32_t biggest = 0u;
                uint32_t min_base = UINT32_MAX;
                uint32_t max_base = 0u;
                /* Buckets over |delta| between consecutive triangles' first
                 * index: <64, <1K, <8K, <64K, >=64K. Only the last forces a
                 * new draw call. */
                uint64_t b[5] = { 0, 0, 0, 0, 0 };
                uint32_t arena_verts = renderer->groups[TRSPK_VBO_GROUP_STATIC].vbo_cpu
                                           ? renderer->groups[TRSPK_VBO_GROUP_STATIC]
                                                 .vbo_cpu->vertex_count
                                           : 0u;
                dumped = 1;
                for( uint32_t ci = 0u; ci < chunk_count; ++ci )
                {
                    const uint32_t tri = renderer->chunks[ci].index_count / 3u;
                    const uint32_t bv = renderer->chunks[ci].base_vertex;
                    if( bv < min_base )
                        min_base = bv;
                    if( bv > max_base )
                        max_base = bv;
                    if( tri <= 1u )
                        singleton++;
                    if( tri > biggest )
                        biggest = tri;
                }
                {
                    const struct TRSPK_DrawRange* r =
                        trspk_drawrangelist_head(renderer->draw_ranges);
                    while( r )
                    {
                        for( uint32_t i = r->start + 3u; i + 2u < r->end; i += 3u )
                        {
                            uint32_t a = staging[i];
                            uint32_t c = staging[i - 3u];
                            uint32_t d = a > c ? a - c : c - a;
                            b[d < 64u ? 0 : d < 1024u ? 1 : d < 8192u ? 2 : d < 65536u ? 3 : 4]++;
                        }
                        r = trspk_drawrangelist_next(renderer->draw_ranges, r);
                    }
                }
                fprintf(
                    stderr,
                    "WebGL1 chunks: %u chunks for %u tri = %.1f tri/chunk | 1-tri chunks %u "
                    "| biggest %u tri | bases %u..%u | static arena %u verts\n"
                    "WebGL1 deltas: <64:%llu  <1K:%llu  <8K:%llu  <64K:%llu  >=64K:%llu "
                    "(only >=64K forces a split)\n",
                    chunk_count,
                    written / 3u,
                    chunk_count ? (double)(written / 3u) / (double)chunk_count : 0.0,
                    singleton,
                    biggest,
                    min_base,
                    max_base,
                    arena_verts,
                    (unsigned long long)b[0],
                    (unsigned long long)b[1],
                    (unsigned long long)b[2],
                    (unsigned long long)b[3],
                    (unsigned long long)b[4]);
            }
        }
        if( straddle )
        {
            /* The arena promised no model crosses a page. Say so once rather
             * than every frame: it is a build-time invariant, so if it breaks
             * it breaks for the whole session. */
            static int reported = 0;
            if( !reported )
            {
                reported = 1;
                fprintf(
                    stderr,
                    "WebGL1: %u triangle(s) straddle a %u-vertex arena page and were "
                    "dropped — the model arena is not paged for 16-bit indices\n",
                    straddle,
                    TRSPK_GL3_VBO_PAGE);
            }
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
        glBufferSubData(
            GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)(written * sizeof(uint16_t)),
            renderer->idx16);
        TORIRS_PERF_COUNT(
            TORIRS_PERF_CTR_GL_IBO_UPLOAD_BYTES, (int64_t)(written * sizeof(uint16_t)));
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_IBO_UPLOADS, 1);

        {
            /*
             * Re-point the attributes only when the base actually moves.
             *
             * Attribute state IS the base vertex here — WebGL1 has neither VAOs
             * nor glDrawElementsBaseVertex, so a chunk's base is folded into the
             * glVertexAttribPointer offsets. But consecutive chunks usually
             * share a base: the splitter emits a new chunk per draw range, and
             * ranges are painter-ordered over an arena that mostly sits inside
             * one 65536-vertex window. Re-binding five attribute pointers for a
             * base that did not change is pure driver overhead, and it was
             * happening once per range.
             */
            uint32_t bound_base = UINT32_MAX;
            uint32_t bound_group = UINT32_MAX;

            /* The element buffer is already bound (the upload above used it)
             * and never changes in this loop, so it is not re-bound per draw. */
            for( uint32_t ci = 0u; ci < chunk_count; ++ci )
            {
                const struct TRSPK_WebGL1Chunk* chunk = &renderer->chunks[ci];
                if( chunk->index_count < 3u )
                    continue;
                if( chunk->group != bound_group )
                {
                    /* Group change is rare (there are two) and is the only time
                     * the array buffer and the attribute enables need touching. */
                    gl3_bind_group_attribs(
                        renderer, &renderer->groups[chunk->group], chunk->base_vertex);
                    bound_group = chunk->group;
                    bound_base = chunk->base_vertex;
                    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATTRIB_REBINDS, 1);
                }
                else if( chunk->base_vertex != bound_base )
                {
                    gl3_point_group_attribs(renderer, chunk->base_vertex);
                    bound_base = chunk->base_vertex;
                    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATTRIB_REBINDS, 1);
                }
                glDrawElements(
                    GL_TRIANGLES,
                    (GLsizei)chunk->index_count,
                    GL_UNSIGNED_SHORT,
                    (const void*)(uintptr_t)(chunk->index_start * sizeof(uint16_t)));
                TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, 1);
            }
        }
    }
#else
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
    glBufferSubData(
        GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)(total_indices * sizeof(uint32_t)), staging);
    TORIRS_PERF_COUNT(
        TORIRS_PERF_CTR_GL_IBO_UPLOAD_BYTES, (int64_t)(total_indices * sizeof(uint32_t)));
    TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_IBO_UPLOADS, 1);

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
                TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_ATTRIB_REBINDS, 1);
            }

            glDrawElements(
                GL_TRIANGLES,
                (GLsizei)index_count,
                GL_UNSIGNED_INT,
                (const void*)(uintptr_t)(range->start * sizeof(uint32_t)));
            TORIRS_PERF_COUNT(TORIRS_PERF_CTR_GL_DRAW_CALLS, 1);
        }

        range = trspk_drawrangelist_next(renderer->draw_ranges, range);
    }
#endif

    gl3_unbind_attribs(renderer);

done:
    renderer->has_3d = false;
    renderer->in3d = false;
    /* World pass leaves a world-sized viewport and depth state; restore so any
     * 2D that somehow runs before the next BEGIN_2D is not clipped/occluded. */
    glViewport(renderer->lb_x, renderer->lb_y, renderer->lb_w, renderer->lb_h);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    if( renderer->ibo_chain )
        trspk_ibochain_reset(renderer->ibo_chain);
}


static void
gl3_ev_clear_rect(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    struct ToriRS_RenderCommand_ClearRect const* cmd = &command->u.clear_rect;
    float rgba[4];
    if( !renderer->in2d )
        return;
    trspk_color_argb_to_rgba(TORIRS_GL3_BG, rgba);
    gl3_set_draw_scissor(renderer, 0, 0, renderer->width, renderer->height);
    gl3_draw_textured_quad(renderer, renderer->white_texture, 0, false, NULL,
        (float)cmd->x, (float)cmd->y, (float)(cmd->x + cmd->w), (float)(cmd->y + cmd->h),
        0, 0, 1, 1, rgba);
}

static void
gl3_ev_line(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    struct ToriRS_RenderCommand_Line const* cmd = &command->u.line;
    float rgba[4];
    int thickness;
    int x1;
    int y1;
    int x2;
    int y2;
    if( !renderer->in2d )
        return;
    gl3_set_draw_scissor(renderer, cmd->scissor_x, cmd->scissor_y, cmd->scissor_w, cmd->scissor_h);
    trspk_color_argb_to_rgba(cmd->argb, rgba);
    thickness = cmd->line_width > 0 ? cmd->line_width : 1;
    if( cmd->line_direction )
    {
        x1 = cmd->x; y1 = cmd->y + cmd->h; x2 = cmd->x + cmd->w; y2 = cmd->y;
    }
    else
    {
        x1 = cmd->x; y1 = cmd->y; x2 = cmd->x + cmd->w; y2 = cmd->y + cmd->h;
    }
    gl3_flush_2d_batch(renderer);
    gl3_draw_textured_quad(renderer, renderer->white_texture, 0, false, NULL,
        (float)x1, (float)y1 - (float)(thickness - 1) * 0.5f,
        (float)x2, (float)y1 + (float)thickness * 0.5f, 0, 0, 1, 1, rgba);
    if( y2 != y1 )
        gl3_draw_textured_quad(renderer, renderer->white_texture, 0, false, NULL,
            (float)x2 - (float)thickness * 0.5f, (float)y1,
            (float)x2 + (float)thickness * 0.5f, (float)y2, 0, 0, 1, 1, rgba);
}

static void
gl3_widget_model_transform_vertex(
    struct ToriDraw_WidgetModelTransform const* xf,
    int vx,
    int vy,
    int vz0,
    int* out_cx,
    int* out_cy,
    int* out_cz)
{
    int t;

    if( xf->var3 != 0 )
    {
        t = (vy * xf->var14 + vx * xf->var15) >> 16;
        vy = (vy * xf->var15 - vx * xf->var14) >> 16;
        vx = t;
    }
    if( xf->var10 != 0 )
    {
        t = (vy * xf->var11 - vz0 * xf->var10) >> 16;
        vz0 = (vy * xf->var10 + vz0 * xf->var11) >> 16;
        vy = t;
    }
    if( xf->var2 != 0 )
    {
        t = (vz0 * xf->var12 + vx * xf->var13) >> 16;
        vz0 = (vz0 * xf->var13 - vx * xf->var12) >> 16;
        vx = t;
    }

    vx += xf->var5;
    vy += xf->var6;
    vz0 += xf->var7;

    t = (vy * xf->var17 - vz0 * xf->var16) >> 16;
    vz0 = (vy * xf->var16 + vz0 * xf->var17) >> 16;

    *out_cx = vx;
    *out_cy = t;
    *out_cz = vz0;
}

static bool
gl3_widget_model_project_vertex(
    struct ToriDraw_WidgetModelTransform const* xf,
    int cx,
    int cy,
    int cz,
    int mid_z,
    float origin_x,
    float origin_y,
    float* out_x,
    float* out_y,
    float* out_z)
{
    int px;
    int py;
    int const ortho_scale = xf->zoom2d > 100 ? xf->zoom2d : 100;

    if( xf->orthographic )
    {
        px = (cx * xf->zoom3d) / ortho_scale;
        py = (cy * xf->zoom3d) / ortho_scale;
    }
    else
    {
        if( cz <= GL3_WIDGET_MODEL_NEAR )
            return false;
        px = (cx * xf->zoom3d) / cz;
        py = (cy * xf->zoom3d) / cz;
    }
    *out_x = origin_x + (float)px;
    *out_y = origin_y + (float)py;
    /* Negate mid-relative depth so larger cz (further) maps toward +NDC z with
     * a standard GL ortho (m10 = -2/(far-near)). */
    *out_z = (float)(mid_z - cz);
    return true;
}

static uint32_t
gl3_bake_widget_model(
    struct ToriRS_GL3* renderer,
    struct GL3ModelGroup* g,
    struct ToriDraw_ModelHandle model_handle,
    struct ToriDraw_WidgetModelTransform const* xf,
    float origin_x,
    float origin_y)
{
    int face_count_faces;
    int vc;
    vertexint_t* vertices_x;
    vertexint_t* vertices_y;
    vertexint_t* vertices_z;
    int z_sum = 0;
    int z_count = 0;
    int mid_z = 0;
    uint32_t slot_index;
    struct TRSPK_ModelSlot const* model_slot;
    uint32_t base;
    uint32_t tri_count;

    assert(g->arena && g->vbo_cpu);
    face_count_faces = trspk_toridraw_face_count(model_handle);
    if( face_count_faces <= 0 )
        return UINT32_MAX;

    vc = ToriDraw_ModelGetVertexCount(model_handle);
    vertices_x = ToriDraw_ModelGetVerticesX(model_handle);
    vertices_y = ToriDraw_ModelGetVerticesY(model_handle);
    vertices_z = ToriDraw_ModelGetVerticesZ(model_handle);
    /* Soft3D objRender: midZ = (eyeY*sinPitch + eyeZ*cosPitch) >> 16. Averaging
     * visible cz diverges for chatheads and makes relative depth noisy. */
    if( !xf->orthographic )
        mid_z = (xf->var6 * xf->var16 + xf->var7 * xf->var17) >> 16;
    else
    {
        for( int i = 0; i < vc; i++ )
        {
            int cx;
            int cy;
            int cz;
            gl3_widget_model_transform_vertex(
                xf, vertices_x[i], vertices_y[i], vertices_z[i], &cx, &cy, &cz);
            z_sum += cz;
            z_count++;
        }
        mid_z = z_count > 0 ? z_sum / z_count : 0;
    }

    {
        uint32_t const vert_count = (uint32_t)face_count_faces * 3u;
        slot_index = trspk_modelarena_load(g->arena, GL3_WIDGET_ARENA_ELEMENT_ID, 0, vert_count);
        model_slot = trspk_modelarena_get(g->arena, slot_index);
        if( !model_slot )
            return UINT32_MAX;
        base = model_slot->vertex_base;
        tri_count = (uint32_t)face_count_faces;

        for( uint32_t face_index = 0; face_index < tri_count; face_index++ )
        {
            struct ToriDraw_Scene* ctx = renderer->scene;
            struct TRSPK_ToriDrawBakeFaceVerts face;
            uint32_t const vi = base + face_index * 3u;
            float sx[3];
            float sy[3];
            float sz[3];
            int visible = 0;

            assert(ctx);
            if( !trspk_toridraw_bake_face_handle(
                    model_handle, face_index, NULL, ctx, true, &face) )
                continue;
            /* Soft3D never rasterizes HIDDEN / fully-transparent faces. Leave
             * the slot zeroed (alpha 0) so painter order cannot punch holes. */
            if( face.color_a[3] <= (1.0f / 255.0f) )
                continue;

            {
                int const slot = gl3_ensure_texture(renderer, face.tex_id);
                face.tex_id_encoded = trspk_encode_vertex_tex_id(
                    slot, face.tex_cutout, (int)renderer->tex_cap);
            }
            trspk_triangles_set(&g->triangles, (base / 3u) + face_index, TRSPK_TRIANGLES_ATLAS);

            {
                int const locals[3][3] = {
                    { (int)face.wx_a, (int)face.wy_a, (int)face.wz_a },
                    { (int)face.wx_b, (int)face.wy_b, (int)face.wz_b },
                    { (int)face.wx_c, (int)face.wy_c, (int)face.wz_c },
                };
                bool corner_visible[3] = { false, false, false };
                for( int corner = 0; corner < 3; corner++ )
                {
                    int cx;
                    int cy;
                    int cz;
                    gl3_widget_model_transform_vertex(
                        xf,
                        locals[corner][0],
                        locals[corner][1],
                        locals[corner][2],
                        &cx,
                        &cy,
                        &cz);
                    corner_visible[corner] = gl3_widget_model_project_vertex(
                        xf, cx, cy, cz, mid_z, origin_x, origin_y, &sx[corner], &sy[corner], &sz[corner]);
                    if( corner_visible[corner] )
                        visible++;
                }
                if( visible <= 0 )
                    continue;
                for( int corner = 0; corner < 3; corner++ )
                {
                    if( !corner_visible[corner] )
                    {
                        sx[corner] = origin_x - 5000.0f;
                        sy[corner] = origin_y;
                        sz[corner] = 0.0f;
                    }
                }
            }

            gl3_write_vertex_opengl3(
                g->vbo_cpu,
                vi + 0u,
                sx[0],
                sy[0],
                sz[0],
                face.color_a,
                face.uv.u1,
                face.uv.v1,
                face.tex_id_encoded,
                face.uv_mode);
            gl3_write_vertex_opengl3(
                g->vbo_cpu,
                vi + 1u,
                sx[1],
                sy[1],
                sz[1],
                face.color_b,
                face.uv.u2,
                face.uv.v2,
                face.tex_id_encoded,
                face.uv_mode);
            gl3_write_vertex_opengl3(
                g->vbo_cpu,
                vi + 2u,
                sx[2],
                sy[2],
                sz[2],
                face.color_c,
                face.uv.u3,
                face.uv.v3,
                face.tex_id_encoded,
                face.uv_mode);
        }
    }
    return base;
}

static void
gl3_mat4_ortho_top_left_zf(
    float m[16],
    float left,
    float right,
    float top,
    float bottom,
    float near_z,
    float far_z)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / (right - left);
    /* top < bottom in top-left pixel space (top=0, bottom=height). */
    m[5] = 2.0f / (top - bottom);
    m[10] = -2.0f / (far_z - near_z);
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[14] = -(far_z + near_z) / (far_z - near_z);
    m[15] = 1.0f;
}

static void
gl3_upload_widget_ubo(
    struct ToriRS_GL3* renderer,
    float const* model_view,
    float const* projection)
{
    TRSPK_UboWorld u = { 0 };
    memcpy(u.modelViewMatrix, model_view, sizeof(float) * 16u);
    memcpy(u.projectionMatrix, projection, sizeof(float) * 16u);
    u.uClock = (float)renderer->frame_clock;
    u.uAtlasDim = (float)renderer->atlas_dim;
    u.uAtlasSlots = (float)renderer->tex_cap;
    gl3_set_world_uniforms(renderer, &u);
}

static void
gl3_ev_model_widget(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    struct ToriRS_RenderCommand_ModelWidget const* wcmd = &command->u.model_widget;
    struct ToriDraw_WidgetModelTransform xf;
    struct GL3ModelGroup* g;
    int face_count;
    uint32_t vertex_base;
    float model_view[16];
    float widget_proj[16];
    int scissor_x;
    int scissor_y;
    int scissor_w;
    int scissor_h;
    int gl_x;
    int gl_y;
    int gl_w;
    int gl_h;
    float origin_x;
    float origin_y;
    if( !renderer->in2d || wcmd->model.kind == TORIDRAWMK_NONE )
        return;
    ToriDraw_WidgetModelTransformInit(
        &xf,
        wcmd->model_zoom > 0 ? wcmd->model_zoom : 2000,
        wcmd->model_xan,
        wcmd->model_yan,
        wcmd->model_zan,
        wcmd->model_x_offset,
        wcmd->model_y_offset,
        0,
        wcmd->model_orthog != 0,
        wcmd->model_fixed_zoom != 0);
    g = &renderer->groups[TRSPK_VBO_GROUP_DYNAMIC];
    gl3_reset_model_group(g);
    face_count = trspk_toridraw_face_count(wcmd->model);
    if( face_count <= 0 )
        return;
    origin_x = (float)(wcmd->x + (wcmd->w > 0 ? wcmd->w / 2 : 0));
    origin_y = (float)(wcmd->y + (wcmd->h > 0 ? wcmd->h / 2 : 0));
    vertex_base = gl3_bake_widget_model(renderer, g, wcmd->model, &xf, origin_x, origin_y);
    if( vertex_base == UINT32_MAX )
        return;
    if( !gl3_upload_group(g) )
        return;
    if( trspk_atlas_is_dirty(&renderer->atlas) )
        gl3_upload_atlas_texture(renderer);
    scissor_x = wcmd->scissor_x;
    scissor_y = wcmd->scissor_y;
    scissor_w = wcmd->scissor_w > 0 ? wcmd->scissor_w : wcmd->w;
    scissor_h = wcmd->scissor_h > 0 ? wcmd->scissor_h : wcmd->h;
    trspk_logical_rect_to_framebuffer(scissor_x, scissor_y, scissor_w, scissor_h, renderer->height,
        renderer->lb_x, renderer->lb_y, renderer->lb_w, renderer->lb_h, renderer->width,
        renderer->height, &gl_x, &gl_y, &gl_w, &gl_h);
    gl3_flush_2d_batch(renderer);
    glEnable(GL_SCISSOR_TEST);
    glScissor(gl_x, gl_y, gl_w, gl_h);
    /* Soft3D skips HIDDEN/transparent faces; we still submit them with alpha 0.
     * Depth-writing those would punch holes through the chathead. Painter order
     * via face submission is enough once Z is in range (see ortho near/far). */
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    memset(model_view, 0, sizeof(model_view));
    model_view[0] = model_view[5] = model_view[10] = model_view[15] = 1.0f;
    gl3_mat4_ortho_top_left_zf(
        widget_proj,
        0.0f,
        (float)renderer->width,
        0.0f,
        (float)renderer->height,
        GL3_WIDGET_MODEL_Z_NEAR,
        GL3_WIDGET_MODEL_Z_FAR);
    glViewport(renderer->lb_x, renderer->lb_y, renderer->lb_w, renderer->lb_h);
    gl3_upload_widget_ubo(renderer, model_view, widget_proj);
    gl3_bind_world_draw_state(renderer);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    gl3_bind_group_attribs(renderer, g, 0u);
    glDrawArrays(GL_TRIANGLES, (GLint)vertex_base, (GLsizei)(face_count * 3));
    gl3_unbind_attribs(renderer);
    glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_FALSE);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(renderer->program2d);
    gl3_bind_quad_attribs(renderer);
}

static void
handle_render_command(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand* command)
{
    switch( command->kind )
    {
    case TORIRSRC_TEX_LOAD:
        gl3_ev_tex_load(renderer, command);
        break;

    case TORIRSRC_TEX_UNLOAD:
        gl3_ev_tex_unload(renderer, command);
        break;

    case TORIRSRC_BEGIN_3D:
        gl3_ev_begin_3d(renderer, command);
        break;

    case TORIRSRC_END_3D:
        gl3_ev_end_3d(renderer, command);
        break;

    case TORIRSRC_BEGIN_2D:
        gl3_ev_begin_2d(renderer, command);
        break;

    case TORIRSRC_END_2D:
        gl3_ev_end_2d(renderer, command);
        break;

    case TORIRSRC_CLEAR_RECT:
        gl3_ev_clear_rect(renderer, command);
        break;

    case TORIRSRC_FILL_RECT:
        gl3_ev_fill_rect(renderer, command);
        break;

    case TORIRSRC_SPRITE_LOAD:
        gl3_ev_sprite_load(renderer, command);
        break;

    case TORIRSRC_SPRITE_UNLOAD:
        gl3_ev_sprite_unload(renderer, command);
        break;

    case TORIRSRC_SPRITE:
        gl3_ev_sprite(renderer, command);
        break;

    case TORIRSRC_FONT_LOAD:
        gl3_ev_font_load(renderer, command);
        break;

    case TORIRSRC_FONT_UNLOAD:
        gl3_ev_font_unload(renderer, command);
        break;

    case TORIRSRC_FONT:
        gl3_ev_font(renderer, command);
        break;

    case TORIRSRC_ANIM_LOAD:
        gl3_ev_anim_load(renderer, command);
        break;

    case TORIRSRC_ANIM_UNLOAD:
        gl3_ev_anim_unload(renderer, command);
        break;

    case TORIRSRC_MODEL_LOAD:
        gl3_ev_model_load(renderer, command);
        break;

    case TORIRSRC_MODEL_UNLOAD:
        gl3_ev_model_unload(renderer, command);
        break;

    case TORIRSRC_BATCH3D_BEGIN:
        gl3_ev_batch3d_begin(renderer, command);
        break;

    case TORIRSRC_BATCH3D_MODEL_ADD:
        gl3_ev_batch3d_model_add(renderer, command);
        break;

    case TORIRSRC_BATCH3D_ANIM_ADD:
        gl3_ev_batch3d_anim_add(renderer, command);
        break;

    case TORIRSRC_BATCH3D_END:
        gl3_ev_batch3d_end(renderer, command);
        break;

    case TORIRSRC_BATCH3D_CLEAR:
        gl3_ev_batch3d_clear(renderer, command);
        break;

    case TORIRSRC_DRAW_MODEL:
        gl3_ev_model_draw(renderer, command);
        break;

    case TORIRSRC_DRAW_MODEL_WIDGET:
        gl3_ev_model_widget(renderer, command);
        break;

    case TORIRSRC_LINE:
        gl3_ev_line(renderer, command);
        break;

    case TORIRSRC_NONE:
        break;

    case TORIRSRC_TEX_BEGIN:
    case TORIRSRC_TEX_END:
    case TORIRSRC_SPRITE_BEGIN:
    case TORIRSRC_SPRITE_END:
    case TORIRSRC_FONT_BEGIN:
    case TORIRSRC_FONT_END:
        break;

    default:
        break;
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

struct ToriRS_GL3*
ToriRS_GL3_New(
    int width,
    int height)
{
    struct ToriRS_GL3* renderer =
        (struct ToriRS_GL3*)calloc(
            1, sizeof(struct ToriRS_GL3));
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

    for( int i = 0; i < TRSPK_GL3_FONT_CAP; i++ )
        renderer->font_slots[i].font_id = -1;

    for( int i = 0; i < TORIDRAW_TEXTURE_ID_CAPACITY; i++ )
        renderer->tex_slot_of_id[i] = -1;
    renderer->tex_slot_next = 0;
    memset(renderer->tex_resident, 0, sizeof(renderer->tex_resident));

    /* Prefer a 4096 atlas (1024 slots) so RS2 material ids past 255 fit; fall
     * back when the driver cannot allocate that size. GL context may not exist
     * yet here — probe later at GL bring-up, or use preferred and let
     * glTexImage2D fail into the fallback path below if needed. Default to
     * preferred; ToriRS_GL3_CreateGL overrides after querying MAX_TEXTURE_SIZE. */
    renderer->atlas_dim = TRSPK_GL3_ATLAS_DIM_PREF;
    renderer->atlas_cols = TRSPK_GL3_ATLAS_COLS_PREF;
    renderer->tex_cap = TRSPK_GL3_TEX_CAP_PREF;

    if( !trspk_atlas_init_grid(
            &renderer->atlas,
            renderer->atlas_dim,
            renderer->atlas_dim,
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
ToriRS_GL3_SetViewport(
    struct ToriRS_GL3* renderer,
    int width,
    int height)
{
    if( !renderer || width <= 0 || height <= 0 )
        return;
    if( renderer->width == width && renderer->height == height )
        return;
    renderer->width = width;
    renderer->height = height;
    /* proj2d is rebuilt from width/height at the top of every frame
     * (trspk_mat4_ortho2d_top_left), so there is nothing else to do here. */
}

void
ToriRS_GL3_Free(struct ToriRS_GL3* renderer)
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


    if( renderer->gl_context )
    {
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = NULL;
    }

    free(renderer);
}

bool
ToriRS_GL3_Init(struct ToriRS_GL3* gl3, SDL_Window* window, struct ToriDraw_Scene* scene)
{
    assert(gl3 && window && scene);
    gl3->scene = scene;
    gl3->window = window;
    gl3->gl_context = SDL_GL_CreateContext(window);
    if( !gl3->gl_context )
    {
        fprintf(stderr, "OpenGL3: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }

    if( SDL_GL_MakeCurrent(window, gl3->gl_context) != 0 )
    {
        fprintf(stderr, "OpenGL3: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DeleteContext(gl3->gl_context);
        gl3->gl_context = NULL;
        return false;
    }

#if !defined(TORIRS_GL_ES2)
    /* Desktop GL needs its entry points resolved; emscripten links the GLES2
     * ones straight into the module. */
    if( !trspk_sdlgl_init() )
    {
        fprintf(stderr, "OpenGL3: trspk_sdlgl_init failed\n");
        SDL_GL_DeleteContext(gl3->gl_context);
        gl3->gl_context = NULL;
        return false;
    }
#endif

    SDL_GL_SetSwapInterval(0);

    /* Say what we actually got. The renderer is written against one feature
     * set; if the context is not the one it expects, that is worth seeing on
     * line one rather than deducing from a black screen. */
    {
        GLint max_tex = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
        fprintf(
            stderr,
            "%s: %s | GLSL %s | %s | max texture %d\n",
            TORIRS_GL_BACKEND_NAME,
            (const char*)glGetString(GL_VERSION),
            (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION),
            (const char*)glGetString(GL_RENDERER),
            (int)max_tex);
    }

    /* Prefer 4096 atlas; fall back if the driver cannot take it. */
    {
        GLint max_tex = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
#if defined(TORIRS_GL_ES2)
        /*
         * 2048 on the web, whatever GL_MAX_TEXTURE_SIZE claims.
         *
         * The preferred 4096 atlas is a single 64MB RGBA allocation. Chrome's
         * software rasterizer drops the whole WebGL context rather than failing
         * that upload, which surfaces as every later call reporting "object
         * does not belong to this context" and nothing saying why. A real GPU
         * takes it fine, so this is a software-GL memory limit rather than a
         * WebGL1 one — but 2048 is 16MB for 256 slots, the same fallback the
         * desktop path takes on a small-texture driver, and no cache we ship
         * needs more.
         */
        uint32_t want_dim = TRSPK_GL3_ATLAS_DIM_FALLBACK;
        uint32_t want_cols = TRSPK_GL3_ATLAS_COLS_FALLBACK;
        uint32_t want_cap = TRSPK_GL3_TEX_CAP_FALLBACK;
#else
        uint32_t want_dim = TRSPK_GL3_ATLAS_DIM_PREF;
        uint32_t want_cols = TRSPK_GL3_ATLAS_COLS_PREF;
        uint32_t want_cap = TRSPK_GL3_TEX_CAP_PREF;
#endif
        if( max_tex > 0 && (GLint)want_dim > max_tex )
        {
            want_dim = TRSPK_GL3_ATLAS_DIM_FALLBACK;
            want_cols = TRSPK_GL3_ATLAS_COLS_FALLBACK;
            want_cap = TRSPK_GL3_TEX_CAP_FALLBACK;
            fprintf(
                stderr,
                "OpenGL3: GL_MAX_TEXTURE_SIZE=%d; using %ux%u atlas (%u slots)\n",
                (int)max_tex,
                want_dim,
                want_dim,
                want_cap);
        }
        if( gl3->atlas_dim != want_dim )
        {
            if( trspk_atlas_is_initialized(&gl3->atlas) )
                trspk_atlas_free(&gl3->atlas);
            gl3->atlas_dim = want_dim;
            gl3->atlas_cols = want_cols;
            gl3->tex_cap = want_cap;
            if( !trspk_atlas_init_grid(
                    &gl3->atlas,
                    gl3->atlas_dim,
                    gl3->atlas_dim,
                    TRSPK_ATLAS_TILE,
                    TRSPK_ATLAS_TILE,
                    4u) )
            {
                fprintf(stderr, "OpenGL3: atlas init failed\n");
                goto fail_gl;
            }
        }
    }

#if defined(TORIRS_GL_ES2)
    const char* vs_src = trspk_webgl1_vertex_shader;
    const char* fs_src = trspk_webgl1_fragment_shader;
#else
    const char* vs_src = trspk_opengl3_vertex_shader;
    const char* fs_src = trspk_opengl3_fragment_shader;
#endif
    GLuint vertexShader = gl3_compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fragmentShader = gl3_compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if( vertexShader == 0u || fragmentShader == 0u )
        goto fail_gl;

    gl3->program3d = glCreateProgram();
    if( gl3->program3d == 0u )
        goto fail_gl;

    glAttachShader(gl3->program3d, vertexShader);
    glAttachShader(gl3->program3d, fragmentShader);
    glBindAttribLocation(gl3->program3d, 0u, "a_position");
    glBindAttribLocation(gl3->program3d, 1u, "a_color");
    glBindAttribLocation(gl3->program3d, 2u, "a_texcoord");
    glBindAttribLocation(gl3->program3d, 3u, "a_tex_id");
    glBindAttribLocation(gl3->program3d, 4u, "a_uv_mode");
    glLinkProgram(gl3->program3d);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    vertexShader = 0u;
    fragmentShader = 0u;

    GLint link_ok = 0;
    glGetProgramiv(gl3->program3d, GL_LINK_STATUS, &link_ok);
    if( !link_ok )
    {
        char buf[512];
        glGetProgramInfoLog(gl3->program3d, (GLsizei)sizeof(buf), NULL, buf);
        fprintf(stderr, "OpenGL3: program link failed: %s\n", buf);
        goto fail_gl;
    }
    if( !gl3_check_error("link program") )
        goto fail_gl;

#if defined(TORIRS_GL_ES2)
    /* No uniform blocks in GLES2: the same values, one uniform each. */
    gl3->u_model_view = glGetUniformLocation(gl3->program3d, "u_modelViewMatrix");
    gl3->u_projection = glGetUniformLocation(gl3->program3d, "u_projectionMatrix");
    gl3->u_clock = glGetUniformLocation(gl3->program3d, "uClock");
    gl3->u_atlas_dim = glGetUniformLocation(gl3->program3d, "uAtlasDim");
    gl3->u_atlas_slots = glGetUniformLocation(gl3->program3d, "uAtlasSlots");
    if( gl3->u_model_view < 0 || gl3->u_projection < 0 )
    {
        fprintf(stderr, "WebGL1: program3d is missing its matrix uniforms\n");
        goto fail_gl;
    }
#else
    const GLuint block_ix = glGetUniformBlockIndex(gl3->program3d, "TRSPK_UboWorld");
    if( block_ix != GL_INVALID_INDEX )
        glUniformBlockBinding(gl3->program3d, block_ix, 0u);

    glGenBuffers(1, &gl3->ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, gl3->ubo);
    glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)sizeof(TRSPK_UboWorld), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
#endif

    gl3->a_position = glGetAttribLocation(gl3->program3d, "a_position");
    gl3->a_color = glGetAttribLocation(gl3->program3d, "a_color");
    gl3->a_texcoord = glGetAttribLocation(gl3->program3d, "a_texcoord");
    gl3->a_tex_id = glGetAttribLocation(gl3->program3d, "a_tex_id");
    gl3->a_uv_mode = glGetAttribLocation(gl3->program3d, "a_uv_mode");
    gl3->s_atlas = glGetUniformLocation(gl3->program3d, "s_atlas");

    glGenBuffers(1, &gl3->ebo);

    for( uint32_t gi = 0u; gi < TRSPK_VBO_GROUP_COUNT; ++gi )
    {
        struct GL3ModelGroup* g = &gl3->groups[gi];
        glGenBuffers(1, &g->vbo_gpu);
#if !defined(TORIRS_GL_ES2)
        glGenVertexArrays(1, &g->vao);
        glBindVertexArray(g->vao);
#endif
        glBindBuffer(GL_ARRAY_BUFFER, g->vbo_gpu);
        glBufferData(
            GL_ARRAY_BUFFER,
            TRSPK_GL3_GPU_VBO_INIT * sizeof(struct TRSPK_VertexOpenGl3),
            NULL,
            GL_DYNAMIC_DRAW);
        bind_vbo_attribs(gl3, g->vbo_gpu, 0u);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl3->ebo);
        gl3_unbind_attribs(gl3);
        g->gpu_capacity = TRSPK_GL3_GPU_VBO_INIT;
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl3->ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER, TRSPK_GL3_GPU_IBO_INIT * TORIRS_GL_INDEX_SIZE, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    gl3->gpu_ibo_capacity = TRSPK_GL3_GPU_IBO_INIT;

    glGenTextures(1, &gl3->atlas_texture);
    glBindTexture(GL_TEXTURE_2D, gl3->atlas_texture);
    /* TORIRS_GL_TEX_RGBA (sized internal format) is required on Core Profile / Metal-backed GL. */
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        TORIRS_GL_TEX_RGBA,
        (GLsizei)gl3->atlas_dim,
        (GLsizei)gl3->atlas_dim,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        gl3->atlas.pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if( !gl3_batch2d_init(gl3) )
    {
        fprintf(stderr, "OpenGL3: failed to allocate 2D batch buffer\n");
        goto fail_gl;
    }

    {
#if defined(TORIRS_GL_ES2)
        GLuint vs2d = gl3_compile_shader(GL_VERTEX_SHADER, trspk_webgl1_2d_vertex_shader);
        GLuint fs2d = gl3_compile_shader(GL_FRAGMENT_SHADER, trspk_webgl1_2d_fragment_shader);
#else
        GLuint vs2d = gl3_compile_shader(GL_VERTEX_SHADER, trspk_opengl3_2d_vertex_shader);
        GLuint fs2d = gl3_compile_shader(GL_FRAGMENT_SHADER, trspk_opengl3_2d_fragment_shader);
#endif
        if( vs2d && fs2d )
        {
            gl3->program2d = glCreateProgram();
            glAttachShader(gl3->program2d, vs2d);
            glAttachShader(gl3->program2d, fs2d);
            glBindAttribLocation(gl3->program2d, 0, "a_position");
            glBindAttribLocation(gl3->program2d, 1, "a_texcoord");
            glBindAttribLocation(gl3->program2d, 2, "a_color");
            glLinkProgram(gl3->program2d);
            GLint link_ok = 0;
            glGetProgramiv(gl3->program2d, GL_LINK_STATUS, &link_ok);
            if( !link_ok )
            {
                char buf[512];
                glGetProgramInfoLog(gl3->program2d, (GLsizei)sizeof(buf), NULL, buf);
                fprintf(stderr, "OpenGL3: program2d link failed: %s\n", buf);
                glDeleteProgram(gl3->program2d);
                gl3->program2d = 0u;
            }
            else
            {
                gl3->u2d_projection =
                    glGetUniformLocation(gl3->program2d, "u_projection");
                gl3->u2d_texture = glGetUniformLocation(gl3->program2d, "u_texture");
                gl3->u2d_text_mode = glGetUniformLocation(gl3->program2d, "u_text_mode");
                gl3->u2d_uv_clamp = glGetUniformLocation(gl3->program2d, "u_uv_clamp");
                gl3->u2d_uv_bounds = glGetUniformLocation(gl3->program2d, "u_uv_bounds");
                if( gl3->u2d_projection < 0 || gl3->u2d_texture < 0 ||
                    gl3->u2d_text_mode < 0 )
                {
                    fprintf(stderr,
                        "OpenGL3: program2d missing required uniforms "
                        "(projection=%d texture=%d text_mode=%d)\n",
                        gl3->u2d_projection,
                        gl3->u2d_texture,
                        gl3->u2d_text_mode);
                    glDeleteProgram(gl3->program2d);
                    gl3->program2d = 0u;
                }
            }
            glDeleteShader(vs2d);
            glDeleteShader(fs2d);
        }
        if( !gl3->program2d )
        {
            fprintf(stderr, "OpenGL3: failed to create 2D program\n");
            goto fail_gl;
        }
#if !defined(TORIRS_GL_ES2)
        glGenVertexArrays(1, &gl3->quad_vao);
        gl3_bind_quad_attribs(gl3);
#endif
        glGenBuffers(1, &gl3->quad_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, gl3->quad_vbo);
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
        gl3_unbind_attribs(gl3);

        {
            uint32_t const white_pixel = 0xFFFFFFFFu;
            glGenTextures(1, &gl3->white_texture);
            glBindTexture(GL_TEXTURE_2D, gl3->white_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(
                GL_TEXTURE_2D, 0, TORIRS_GL_TEX_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white_pixel);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
    return true;

fail_gl:
    if( vertexShader )
        glDeleteShader(vertexShader);
    if( fragmentShader )
        glDeleteShader(fragmentShader);
    gl3_destroy_gl_resources(gl3);
    SDL_GL_DeleteContext(gl3->gl_context);
    gl3->gl_context = NULL;
    return false;
}

void
ToriRS_GL3_SetPick(struct ToriRS_GL3* gl3, int mouse_x, int mouse_y)
{
    assert(gl3);
    gl3->pick_enabled = true;
    gl3->pick_mouse_x = mouse_x;
    gl3->pick_mouse_y = mouse_y;
    ToriRS_PickHitsReset(&gl3->pick_hits);
}

struct ToriRS_PickHits const*
ToriRS_GL3_PickHits(struct ToriRS_GL3 const* gl3)
{
    assert(gl3);
    return &gl3->pick_hits;
}

void
ToriRS_GL3_Execute(struct ToriRS_GL3* gl3, struct ToriRS_RenderCommand const* cmd)
{
    assert(gl3);
    assert(cmd);
    handle_render_command(gl3, (struct ToriRS_RenderCommand*)cmd);
}

void
ToriRS_GL3_DrawBootBar(struct ToriRS_GL3* gl3, int progress)
{
    assert(gl3);
    if( progress < 0 )
        progress = 0;
    if( progress > 100 )
        progress = 100;

    SDL_GL_MakeCurrent(gl3->window, gl3->gl_context);

    int drawable_w = gl3->width;
    int drawable_h = gl3->height;
    SDL_GL_GetDrawableSize(gl3->window, &drawable_w, &drawable_h);

    struct TRSPK_Letterbox lb;
    trspk_compute_letterbox(gl3->width, gl3->height, drawable_w, drawable_h, &lb);
    gl3->lb_x = lb.x;
    gl3->lb_y = lb.y;
    gl3->lb_w = lb.w;
    gl3->lb_h = lb.h;

    glViewport(0, 0, drawable_w, drawable_h);
    glClearColor(0.125f, 0.141f, 0.157f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(gl3->program2d);
    glViewport(gl3->lb_x, gl3->lb_y, gl3->lb_w, gl3->lb_h);
    trspk_mat4_ortho2d_top_left(gl3->proj2d, 0.0f, (float)gl3->width, (float)gl3->height, 0.0f);
    glUniformMatrix4fv(gl3->u2d_projection, 1, GL_FALSE, gl3->proj2d);
    gl3_bind_quad_attribs(gl3);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl3->white_texture);
    glUniform1i(gl3->u2d_texture, 0);
    if( gl3->u2d_text_mode >= 0 )
        glUniform1i(gl3->u2d_text_mode, 0);
    if( gl3->u2d_uv_clamp >= 0 )
        glUniform1i(gl3->u2d_uv_clamp, 0);
    glDisable(GL_SCISSOR_TEST);

    int const bar_w = gl3->width / 3;
    int const bar_h = 12;
    int const bar_x = (gl3->width - bar_w) / 2;
    int const bar_y = (gl3->height - bar_h) / 2;
    int const fill_w = bar_w * progress / 100;
    float const border_rgba[4] = { 0.545f, 0.0f, 0.0f, 1.0f };
    float const fill_rgba[4] = { 0.545f, 0.0f, 0.0f, 1.0f };
    float const empty_rgba[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    gl3_flush_2d_batch(gl3);
    gl3_draw_textured_quad_immediate(gl3, (float)(bar_x - 1), (float)(bar_y - 1), (float)(bar_x + bar_w + 1), (float)(bar_y + bar_h + 1), 0, 0, 1, 1, border_rgba);
    gl3_draw_textured_quad_immediate(gl3, (float)bar_x, (float)bar_y, (float)(bar_x + fill_w), (float)(bar_y + bar_h), 0, 0, 1, 1, fill_rgba);
    if( fill_w < bar_w )
        gl3_draw_textured_quad_immediate(gl3, (float)(bar_x + fill_w), (float)bar_y, (float)(bar_x + bar_w), (float)(bar_y + bar_h), 0, 0, 1, 1, empty_rgba);
    gl3_unbind_attribs(gl3);
}

void
ToriRS_GL3_RenderFrame(struct ToriRS_GL3* gl3, struct ToriRS_Frame* frame)
{
    assert(gl3);
    assert(frame);

    SDL_GL_MakeCurrent(gl3->window, gl3->gl_context);

    int drawable_w = gl3->width;
    int drawable_h = gl3->height;
    SDL_GL_GetDrawableSize(gl3->window, &drawable_w, &drawable_h);

    {
        struct TRSPK_Letterbox lb;
        trspk_compute_letterbox(gl3->width, gl3->height, drawable_w, drawable_h, &lb);
        gl3->lb_x = lb.x;
        gl3->lb_y = lb.y;
        gl3->lb_w = lb.w;
        gl3->lb_h = lb.h;
    }

    glViewport(0, 0, drawable_w, drawable_h);
    glClearColor(0.125f, 0.141f, 0.157f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gl3->in3d = false;
    gl3->has_3d = false;
    gl3->frame_clock += 1.0;

    ToriRS_FrameBegin(frame);
    struct ToriRS_RenderCommand command;
    while( ToriRS_FrameNextCommand(frame, &command) )
        ToriRS_GL3_Execute(gl3, &command);
    ToriRS_FrameEnd(frame);

    /* TORIRS_GL3_READBACK=path dumps one GL frame (after READBACK_FRAME, default 90). */
    {
        char const* path = getenv("TORIRS_GL3_READBACK");
        static int done = 0;
        long const want = getenv("TORIRS_GL3_READBACK_FRAME")
                              ? atol(getenv("TORIRS_GL3_READBACK_FRAME"))
                              : 90;
        if( path && path[0] && !done && gl3->frame_clock >= (double)want )
        {
            int fb_w = 0;
            int fb_h = 0;
            int* fb;
            done = 1;
            SDL_GL_GetDrawableSize(gl3->window, &fb_w, &fb_h);
            fb = (int*)malloc((size_t)fb_w * (size_t)fb_h * sizeof(int));
            if( fb )
            {
                int* top = (int*)malloc((size_t)gl3->width * (size_t)gl3->height * sizeof(int));
                void bmp_write_file(const char* filename, int* px, int w, int h);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
#if !defined(TORIRS_GL_ES2)
                glReadBuffer(GL_BACK);
#endif
                glReadPixels(0, 0, fb_w, fb_h, TORIRS_GL_READ_FORMAT, GL_UNSIGNED_BYTE, fb);
                if( top )
                {
                    float const sx = (float)gl3->lb_w / (float)gl3->width;
                    float const sy = (float)gl3->lb_h / (float)gl3->height;
                    int resident = 0;
                    for( int y = 0; y < gl3->height; y++ )
                    {
                        int src_y = gl3->lb_y + (int)((float)(gl3->height - 1 - y) * sy);
                        if( src_y < 0 )
                            src_y = 0;
                        if( src_y >= fb_h )
                            src_y = fb_h - 1;
                        for( int x = 0; x < gl3->width; x++ )
                        {
                            int src_x = gl3->lb_x + (int)((float)x * sx);
                            if( src_x < 0 )
                                src_x = 0;
                            if( src_x >= fb_w )
                                src_x = fb_w - 1;
                            top[y * gl3->width + x] = fb[src_y * fb_w + src_x];
                        }
                    }
                    for( int i = 0; i < (int)gl3->tex_cap; i++ )
                        resident += gl3->tex_resident[i] ? 1 : 0;
                    bmp_write_file(path, top, gl3->width, gl3->height);
                    fprintf(
                        stderr,
                        "gl3_readback: wrote %s tex_resident=%d\n",
                        path,
                        resident);
                    free(top);
                }
                free(fb);
            }
        }
    }
}
