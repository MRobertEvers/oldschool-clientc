#ifndef SRC_PLATFORM_PLATFORM_RENDERER_ES3_CORE_H
#define SRC_PLATFORM_PLATFORM_RENDERER_ES3_CORE_H

/**
 * Shared surface between the WebGL2 renderer's translation units.
 *
 *   platform_renderer_es3_core.c     the context, the programs, the
 *                                              world texture atlas, every
 *                                              retained CPU/GPU vertex buffer,
 *                                              the per-frame index stream and
 *                                              the command dispatch
 *   platform_renderer_es3_ui.c       the 2D stack: sprite atlas,
 *                                              fonts, rects, lines, polygons,
 *                                              rotated-masked chrome, widget
 *                                              models, the boot bar
 *   platform_renderer_es3_painter.c  the painter's-algorithm world
 *                                              ordering; no depth buffer
 *   platform_renderer_es3_zbuffer.c  the hardware depth ordering:
 *                                              a material pre-pass, array
 *                                              ranges for uniform poses, and
 *                                              a sorted blended pass
 *
 * The split and the contract are the D3D9 renderer's
 * (platform_win32_renderer_d3d9_core.h): ToriRS_ES3_Init picks a world
 * implementation by creating (or not creating) the depth state, ::zbuffer is
 * that state and doubles as the selector, and the core calls es3_painter_*
 * or es3_zbuffer_* directly. The two world paths are peers; neither calls
 * the other.
 *
 * ## What this renderer uses that the WebGL1 one cannot
 *
 * The GLES2 renderer (platform_androidarmv7_renderer_opengles2_*.c) is written to OpenGL ES
 * 2.0 core with no extension, because that is exactly WebGL1 and exactly a
 * 2013 phone. This one starts from a WebGL2 context -- OpenGL ES 3.0 core,
 * shipped by every browser since 2021 and the only GL the 2024+ browsers
 * this lane targets need -- and every place the list below names is a place
 * the ES2 ceiling cost the ES2 renderer real per-frame work:
 *
 *   32-bit indices          GL_UNSIGNED_INT elements. An index reaches the
 *                           whole buffer, so geometry is no longer addressed
 *                           in 65,536-vertex pages and a draw is no longer
 *                           ended by crossing one. This is the big one: it
 *                           is what lets the painter path INDEX the retained
 *                           world instead of gathering it (see
 *                           platform_renderer_es3_painter.c) and what
 *                           collapses a settled scene to a handful of draws.
 *   vertex array objects    one VAO per (layout, buffer) pair, built once and
 *                           bound by name. The ES2 renderer re-issues four
 *                           glVertexAttribPointer calls whenever the stream
 *                           or the page changes; in a browser each of those
 *                           is a JavaScript call across the GL boundary.
 *   uniform buffer objects  the world pass's matrix and clock live in one
 *                           std140 block bound to a binding point, uploaded
 *                           once per pass and shared by all four world
 *                           programs, instead of a glUniformMatrix4fv per
 *                           program per pass.
 *   integer attributes      the vertex's tile/scroll word is read with
 *                           glVertexAttribIPointer as a uvec4, so the shader
 *                           does no de-normalising multiply.
 *   GL_UNPACK_ROW_LENGTH    a sub-rectangle of a wider CPU atlas uploads in
 *                           place. The ES2 renderer packs the rows into a
 *                           tight staging buffer first, which is a full copy
 *                           of every texture and every sprite it uploads.
 *   sized internal formats  GL_RGBA8 and GL_R8, with the format the texture
 *                           really has stated rather than inferred.
 *   glDrawRangeElements     the draw states the vertex range it touches, so
 *                           the driver can bound the vertex fetch instead of
 *                           scanning the index block for it.
 *   single-channel textures a font atlas is one byte a texel (GL_R8), where
 *                           ES2 has to spend two on GL_LUMINANCE_ALPHA. Not
 *                           via GL_TEXTURE_SWIZZLE_*, which ES 3.0 has and
 *                           WebGL2 does not: the UI fragment shader builds
 *                           (1, 1, 1, coverage) from the fetch instead.
 *   glInvalidateFramebuffer the offscreen client-scaling target is dropped
 *                           once its present has consumed it, so a
 *                           tile-based GPU never writes those tiles back.
 *   a clipped readback      ToriRS_ES3_ReadPixels reads the letterbox
 *                           rectangle and nothing else. The ES2 renderer
 *                           reads the whole drawable, black bars included,
 *                           because without GL_PACK_ROW_LENGTH the rows of a
 *                           sub-rectangle cannot be packed in one call.
 *   GL_DEPTH_COMPONENT24    guaranteed as a renderbuffer format, so the
 *                           offscreen depth matches the window's rather
 *                           than dropping to the 16 bits ES2 guarantees.
 *
 * WebGL2 is not all of OpenGL ES 3.0, and the gap matters here: texture
 * swizzle is the piece of ES 3.0 this renderer would otherwise have used and
 * cannot. A glTexParameteri naming GL_TEXTURE_SWIZZLE_* does not fail the
 * build or the context -- it is an INVALID_ENUM at runtime and the texture
 * silently keeps the default swizzle, which is why the browser run that found
 * it is in the verification for WEB-GL2-000.
 *
 * What it deliberately does NOT use: any extension (the build turns
 * emscripten's automatic extension enablement off, as the WebGL1 lane does),
 * transform feedback, instancing, texture arrays, or 3D textures. The bake
 * pipeline is shared with the GLES2 renderer down to the 28-byte vertex
 * (TRSPK_VertexGLES2, 3rd/trspk/gles2/gles2_vertex.h), so a fix to a bake
 * lands on both lanes; only the GL layer differs.
 *
 * ## One atlas, one texture bind per pass
 *
 * Every world texture -- scrolling ones included -- lives in the single
 * 2048x2048 atlas. Which tile a face samples and how it scrolls travel with
 * the vertex (TRSPK_VertexGLES2), and the fragment shader wraps and clamps the
 * local coordinate into the tile per fragment. So the world pass binds the
 * atlas once, the index stream is never split by texture, and the draw loop
 * has exactly two reasons to issue a new draw call: a page change, and the
 * plain/cutout program boundary.
 */

#include "platform/client_scale.h"
#include "platform/platform_renderer_es3.h"

#include "core/trspk_atlas.h"
#include "core/trspk_batch16.h"
#include "core/trspk_ibo.h"
#include "core/trspk_modelarena.h"
#include "core/trspk_pose.h"
#include "core/trspk_triangles.h"
#include "core/trspk_vbo.h"
#include "render/torirs_frame.h"
#include "render/torirs_polygon.h"
#include "render/trspk_toridraw.h"

#include "toridraw_font.h"
#include "toridraw_model.h"
#include "toridraw_scene.h"
#include "toridraw_types.h"

#include <GLES3/gl3.h>

#include <stdbool.h>
#include <stdint.h>

/* The world atlas: 16x16 tiles of 128, 256 slots, slot 0 an opaque white
 * tile for untextured faces. 2048 is the floor every WebGL2 device this lane
 * runs on reports for GL_MAX_TEXTURE_SIZE; Init refuses a device below it. */
#define ES3_ATLAS_DIM 2048u
#define ES3_ATLAS_COLS 16u
#define ES3_ATLAS_SLOTS 256u
/*
 * The model arena's page. A slot never crosses a page boundary, which under
 * 16-bit indexing was what made every model reachable from one attribute
 * binding -- and cost a pad every time a model straddled one. A 32-bit index
 * reaches the whole buffer, so the page exists here only as an allocator
 * bound, and it is set far above any model's vertex count: no arena slot is
 * ever padded, and no draw is ever ended by a page edge.
 */
#define ES3_VBO_PAGE (1u << 28)
#define ES3_GPU_BUFFER_INIT 4096u
/*
 * Per-frame GPU streams are SETS of buffers, one per frame in flight, written
 * in rotation: a frame appends only into the buffer the GPU last read
 * ES3_FRAMES_IN_FLIGHT frames ago, so no glBufferSubData ever lands on a
 * buffer with a draw outstanding. That matters more than it sounds on the
 * driver this lane runs on: an update to a busy buffer is "ghosted" -- the
 * driver allocates a fresh store and copies -- and a ring that was appended
 * every frame measured as ~400 page faults a frame, a quarter of the CPU, all
 * of it inside the driver's memcpy. A rotated set never triggers it, and a
 * buffer's pages stay resident from one use to the next.
 */
#define ES3_FRAMES_IN_FLIGHT 4u
#define ES3_INDEX_STREAM_INIT_BYTES (512u * 1024u)
#define ES3_DYNAMIC_STREAM_INIT_BYTES (1024u * 1024u)
#define ES3_DRAW_ITEM_INIT 1024u
/* How often a rotmask slot re-hashes its source sprite for a content change:
 * every Nth frame, staggered per slot. A minimap refresh lands at most N
 * frames late, and the hash -- a full read of the sprite -- costs 1/N.
 * With TORIRS_ES3_ROTMASK_GEN (the default) the hash is only a debug
 * cross-check of the generation counter; see es3_ui_rotmask_upload_source. */
#define ES3_ROTMASK_HASH_PERIOD 8u
#define ES3_UI_ATLAS_DIM 2048u
#define ES3_UI_SPRITE_CAP 2048
#define ES3_UI_VARIANT_CAP 2048u
#define ES3_UI_FONT_CAP 32
#define ES3_UI_BATCH_MAX_VERTS 32768u
/* The 2D stream's opening size; a frame's UI is many small flushes (or, with
 * TORIRS_ES3_UI_DEFER, one upload of the whole pass). */
#define ES3_UI_STREAM_INIT_BYTES (256u * 1024u)
/* The deferred 2D pass: opening sizes of the CPU vertex and record arrays. */
#define ES3_UI_PASS_INIT_VERTICES 8192u
#define ES3_UI_PASS_INIT_RECORDS 64u
#define ES3_UI_ROTMASK_INIT_CAP 8u
#define ES3_WIDGET_MODEL_NEAR 50.0f
#define ES3_WORLD_NEAR 50.0f
#define ES3_WORLD_FAR 32768.0f

#define ES3_WIDGET_CONFIG_ATLAS (-1)
#define ES3_WIDGET_CONFIG_NONE (-2)
#define ES3_WIDGET_CONFIG_SKIP (-3)

/* Vertex stream bindings: the two arena groups, Batch16's page buffer, and
 * the per-frame ordered stream the actors are baked into.
 *
 * There is no resident-window binding here. The GLES2 renderer has one
 * because its painter path cannot index the retained world -- a U16 index
 * reaches 65,536 vertices from wherever the attributes are bound, and
 * painter order hops chunks tile by tile -- so it copies each drawn model
 * into a ring it CAN index. With 32-bit indices the retained buffers are
 * directly indexable and the ring has nothing to do. */
#define ES3_STATIC_PAGE_BINDING TRSPK_VBO_GROUP_COUNT
#define ES3_FRAME_STREAM_BINDING (TRSPK_VBO_GROUP_COUNT + 1u)
#define ES3_BINDING_COUNT (TRSPK_VBO_GROUP_COUNT + 2u)
/* The painter path's frame stream: a ~40k-vertex world at 28 bytes. */
#define ES3_FRAME_STREAM_INIT_BYTES (49152u * 28u)
/* A batch pose locates its Batch16 ENTRY: the batch slot and the entry's
 * index in it, from which the chunk, the vertex base and the GPU page all
 * follow. An entry is the unit the painter keeps residency for. */
#define ES3_BATCH_POSE_FLAG 0x80000000u
#define ES3_BATCH_POSE_SLOT_SHIFT 24u
#define ES3_BATCH_POSE_SLOT_MASK 0x7fu
#define ES3_BATCH_POSE_ENTRY_MASK 0xffffffu
#define ES3_BATCH_PAGE_LIMIT 32768u

/* Attribute locations, the same in every program. */
#define ES3_ATTRIB_POSITION 0u
#define ES3_ATTRIB_TEXCOORD 1u
#define ES3_ATTRIB_COLOR 2u
#define ES3_ATTRIB_TEXINFO 3u
#define ES3_ATTRIB_MASK_TEXCOORD 3u

/*
 * The frame's draw sequence.
 *
 * One ordered list of draws, built by whichever world path is active and
 * issued once at the end of the pass. Two kinds of item share it:
 *
 *   indexed   a run of U32 indices into one binding's whole buffer; the
 *             indices are appended to one staging array and go to the GPU in
 *             a single ring upload. There is no page: an index addresses the
 *             buffer, so the only thing that ends an indexed run is the
 *             binding or the program changing;
 *   array     a contiguous run of vertices in a binding, drawn with
 *             glDrawArrays -- no indices at all. A dynamic model baked in its
 *             sorted face order is one of these; on the depth path so is every
 *             uniform opaque pose.
 *
 * Consecutive compatible items merge as they are pushed: two indexed items on
 * the same binding and program become one draw, two array items that abut
 * become one draw. So the number of glDrawElements/glDrawArrays a frame issues
 * is the number of times the stream or the program genuinely changed -- which,
 * for a settled static world drawn in painter order, is once.
 */
struct ES3DrawItem
{
    uint32_t binding;
    /** Indexed: the lowest and highest vertex any of these indices names,
     *  which is what glDrawRangeElements wants -- the driver can bound the
     *  vertex fetch instead of scanning the index block for it. Both zero on
     *  an array item. */
    uint32_t index_min;
    uint32_t index_max;
    /** Indexed: offset into the staged indices. Array: the first vertex. */
    uint32_t first;
    uint32_t count;
    uint8_t indexed;
    uint8_t cutout;
    /** Depth path only: drawn with blending on and depth writes off. */
    uint8_t blended;
};

/* The per-triangle config word (TRSPK_Triangles): the texture id the face was
 * baked against, or -1 for an untextured face. Kept so a texture that arrives
 * AFTER its faces were baked and turns out to scroll can find them again and
 * patch their anim bytes (es3_refresh_texture_animation). */
#define ES3_TRIANGLE_UNTEXTURED (-1)

/** A framebuffer-space rectangle: GL origin, y up. */
struct ES3Rect
{
    int x;
    int y;
    int width;
    int height;
};

/* A clip in LOGICAL pixels, half-open. Axis-aligned UI quads are clipped
 * against it on the CPU (es3_ui_append_quad_clipped), so no glScissor call
 * -- and no batch break -- stands between two quads with different clips. */
struct ES3Clip
{
    int x0;
    int y0;
    int x1;
    int y1;
};

/** One per-frame GPU stream: ES3_FRAMES_IN_FLIGHT buffers used in rotation,
 *  each appended from offset zero during its frame. */
struct ES3StreamSet
{
    GLuint buffers[ES3_FRAMES_IN_FLIGHT];
    uint32_t capacities[ES3_FRAMES_IN_FLIGHT];
    /** Bytes appended into this frame's buffer so far. */
    uint32_t head;
};

/** The 2D stream vertex. 24 bytes: (x, y, w), (u, v), RGBA bytes. */
/*
 * The UI vertex. `sel` picks what the fragment multiplies the colour by --
 * 0 the sprite atlas on unit 0, 1 the batch's own texture on unit 1 (a font,
 * a widget's texture), 2 nothing (a flat fill) -- so sprites, text and fills
 * share one draw instead of breaking the batch at every texture change.
 */
struct ES3VertexUI
{
    float x;
    float y;
    float w;
    float u;
    float v;
    uint32_t rgba;
    float sel;
};

/** The rotmask vertex: the UI vertex plus the axis-aligned mask coordinate. */
struct ES3VertexRotmask
{
    float x;
    float y;
    float w;
    float u;
    float v;
    uint32_t rgba;
    float mask_u;
    float mask_v;
};

struct ES3Program
{
    GLuint id;
    GLint u_matrix;
    GLint u_texture;
    GLint u_mask;
    GLint u_mask_invert;
    /** The index of the shared WorldBlock in this program, or
     *  GL_INVALID_INDEX for the 2D programs, which have none. */
    GLuint world_block;
};

/*
 * One scene sprite id's place in the UI atlas. `tiles` is kept across an
 * invalidate so a sprite REPLACED over a live id (the title flames, every
 * 35 ms) overwrites its own tile instead of consuming a fresh one until the
 * append-only packer is exhausted.
 */
struct ES3UISpriteTile
{
    uint32_t x;
    uint32_t y;
    uint32_t w; /* padded, as inserted */
    uint32_t h;
    uint8_t valid;
};

struct ES3UISpriteSlot
{
    int scene_id;
    int count;
    float* uvs;
    uint8_t* loaded;
    struct ES3UISpriteTile* tiles;
};

struct ES3UISpriteVariant
{
    int scene_id;
    int atlas_index;
    int outline;
    int graphic_shadow;
    int angle;
    uint8_t flip_h;
    uint8_t flip_v;
    uint8_t if3_transform;
    float u0;
    float v0;
    float u1;
    float v1;
    int ox;
    int oy;
    int width;
    int height;
    bool valid;
};

struct ES3UIFontSlot
{
    int font_id;
    struct ToriDraw_Font* font;
    GLuint texture;
    int texture_width;
    int texture_height;
    float glyph_uv[TORIDRAW_FONT_GLYPH_COUNT * 4];
    bool baked;
};

struct ES3UIRotmaskSlot
{
    int scene_id;
    int atlas_index;
    int mask_scene_id;
    int mask_atlas_index;
    int width;
    int height;
    int source_width;
    int source_height;
    GLuint source_texture;
    GLuint mask_texture;
    /* Content hash of the sprite the texture was last uploaded from; a frame
     * whose sprite pixels are byte-identical skips the upload entirely. */
    uint32_t source_hash;
    uint32_t mask_hash;
    bool source_hash_valid;
    bool mask_hash_valid;
    /* TORIRS_ES3_ROTMASK_GEN: the rotmask source generation
     * (es3_rotmask_source_generation) each texture was uploaded at; 0 =
     * never. A slot re-uploads when the producer has bumped it since. */
    uint32_t source_generation;
    uint32_t mask_generation;
    bool used;
};

/* The vertex layouts a 2D draw record can name (ES3UIDrawRecord.layout). */
#define ES3_UI_RECORD_LAYOUT_UI 0u
#define ES3_UI_RECORD_LAYOUT_ROTMASK 1u
/* A widget model's triangles: the UI vertex layout over the WORLD atlas. */
#define ES3_UI_RECORD_LAYOUT_WIDGET 2u

/*
 * One deferred 2D draw (TORIRS_ES3_UI_DEFER). The pass appends every
 * vertex into one CPU array and records each batch as a RANGE of it plus the
 * state the draw needs; es3_ui_submit uploads the array once, binds the
 * attributes once, and issues glDrawArrays(first, count) per record. The
 * state that GL applies at draw-issue time -- scissor, the two texture units,
 * the program -- is captured here because with deferral the record time and
 * the issue time are no longer the same moment.
 */
struct ES3UIDrawRecord
{
    /** UI layout: first vertex in the pass array. Rotmask layout: first
     *  vertex in the pass's rotmask side array. */
    uint32_t first;
    uint32_t count;
    /** Unit 0 (the sprite atlas, the world atlas for a widget model, or the
     *  rotmask source) and unit 1 (a font, or the rotmask stencil). 0 means
     *  the white texture. A UI-layout record's unit 1 is always a font: see
     *  es3_ui_append_quad_vertices. */
    GLuint texture0;
    GLuint texture1;
    uint8_t layout;
    uint8_t scissor_enabled;
    /** The record samples the UI sprite atlas: it must be uploaded before
     *  the first deferred draw. */
    uint8_t uses_sprite_atlas;
    /** Rotmask only: the u_mask_invert uniform. */
    float mask_invert;
    struct ES3Rect scissor;
};

struct ES3UIBatch
{
    struct ES3VertexUI* vertices;
    uint32_t vertex_count;
    /** Deferred pass: the index in renderer->ui_pass_vertices where this
     *  batch's vertices start (its vertices are appended straight into the
     *  pass array, so `vertices` is unused there). */
    uint32_t first;
    /* Unit 1's texture for the whole batch (0 until a quad names one); unit
     * 0 is always the sprite atlas. A quad naming a different unit-1 texture
     * is the one texture change that still ends a batch. */
    GLuint texture1;
    bool uses_sprite_atlas;
    bool scissor_enabled;
    struct ES3Rect scissor;
};

struct ES3StaticBatch
{
    int batch_id;
    struct TRSPK_Batch16* cpu;
    uint32_t* page_ids;
    uint32_t page_id_capacity;
    bool active;
    bool building;
};

/*
 * One Batch16 chunk's place in the static page buffer.
 *
 * Chunks are packed DENSELY: `gpu_offset` is a vertex offset handed out by a
 * bump allocator over the buffer, and a chunk keeps its range across rebuilds
 * as long as it still fits (`gpu_capacity`). They are not one chunk per
 * 64K-vertex slot, because that spacing decides what the painter can index:
 * a U16 index reaches 65536 vertices from wherever the attributes are bound,
 * and painter order hops chunks tile by tile (measured: 750 chunk changes a
 * frame for 13k faces). Packed, a whole small world lies inside one such
 * window and its faces index; spaced a page apart, every chunk change was a
 * window change and nothing did.
 */
struct ES3StaticPageRef
{
    uint32_t batch_slot;
    uint32_t chunk_index;
    uint32_t gpu_offset;
    uint32_t gpu_capacity;
    /* The chunk's CPU vertex buffer, cached when the page is assigned so the
     * painter's placement does not walk page -> batch -> chunk -> vbo per
     * model. A Batch16 chunk and its VBO object are allocated once and
     * reused across every begin/clear (never freed before the batch is), so
     * the pointer is stable for as long as `valid` is; the vertex array
     * inside it is read through the VBO at use time, never cached, because a
     * rebuild may realloc it. NULL while !valid. */
    const struct TRSPK_VBO* cpu_vbo;
    bool valid;
};

/* The static page buffer starts at one window and doubles. */
#define ES3_STATIC_BATCH_VBO_INIT_VERTICES 65536u

struct ES3ModelGroup
{
    struct TRSPK_VBO* vbo_cpu;
    GLuint vbo_gpu;
    uint32_t gpu_capacity;
    /* A per-frame group lives in the dynamic stream set: vbo_gpu is this
     * frame's buffer and its vertices start at gpu_base_vertex, so every draw
     * into the group is offset by it. Zero for a retained group, whose buffer
     * holds exactly its arena. */
    uint32_t gpu_base_vertex;
    struct TRSPK_ModelArena* arena;
    struct TRSPK_Triangles triangles;
    bool reset_each_frame;
};

struct ES3ZBufferWorld;

/*
 * One model's CPU stage -- what es3_draw_model computes between receiving
 * a DRAW_MODEL command and emitting its faces: the animation pose, the cull
 * verdict, the projection, the pick test against the armed mouse point, and
 * the face sort. Everything in it is a pure function of the command, the
 * scene and the pass camera, and none of it touches GL, which is why it can
 * be computed somewhere other than the draw -- on the second core, one
 * command ahead (platform_renderer_es3_dualcore.c).
 */
struct ES3ModelStage
{
    /** TORIDRAW_CULL_VISIBLE, or the reason the model was rejected. */
    int cull;
    /** The pick test's answer, for the mouse point armed by ToriRS_ES3_SetPick.
     *  Meaningful only when the pick was enabled and the command pickable. */
    bool pick_hit;
    /** Whether face_order / sorted_face_count hold a sort. The painter path
     *  requires one for every visible model; the depth path sorts only a
     *  model with blended faces. */
    bool sorted;
    int sorted_face_count;
    /** The sorted faces, valid until the source is asked again. */
    const int* face_order;
    /** scene->projected_vertex.z after the projection. */
    int projected_depth;
};

/**
 * Where es3_draw_model gets a model's stage from when it does not compute
 * it itself. NULL on the renderer means "compute it here", which is the
 * ordinary --webgl2 lane; the dual-core lane installs one for the duration
 * of its frame.
 *
 * `take` is called for EVERY DRAW_MODEL command the renderer dispatches, in
 * dispatch order, before the draw does anything else with the command -- so
 * a source that computes ahead can pair its results with the asks by count
 * alone. It returns false when it has nothing for this command, and the draw
 * computes the stage itself; a source may do that for the tail of a frame
 * (its storage ran out) and the draw must be indifferent.
 *
 * `begin_3d` is called from es3_begin_3d once the pass camera is set,
 * after every scene event of the frame has been dispatched: the first point
 * at which a producer may safely start reading models for this frame.
 */
struct ES3ModelStageSource
{
    void* user;
    bool (*take)(
        void* user,
        const struct ToriRS_RenderCommand_Model* command,
        struct ES3ModelStage* out);
    void (*begin_3d)(void* user, const struct ToriRS_RenderCommand_Begin3D* command);
};

struct ToriRS_ES3
{
    /* What the lane that built this calls it, for every log line the core
     * writes. @see ToriRS_ES3_New. */
    char const* name;
    struct ToriDraw_Scene* scene;
    /* Projection + face sort; the GPU table has no software raster stage. */
    const struct ToriDraw_Kernel* kernel;
    /** Supplies each model's stage instead of es3_draw_model computing it;
     *  NULL (the default) computes here. See ES3ModelStageSource. */
    const struct ES3ModelStageSource* model_stage_source;
    /** Depth-path models a stage source left unsorted that the material
     *  table then said were blended: projected again on this thread and
     *  sorted. A count for the dual-core lane's debug line; should stay ~0. */
    uint32_t stage_reprojected_models;
    ToriRS_GLWindow* window;
    ToriRS_GLContext gl_context;

    /* The depth renderer's private state, and the mode selector: non-NULL
     * means the es3_zbuffer_* implementation owns the world path. */
    struct ES3ZBufferWorld* zbuffer;

    /* TORIRS_ES3_DEBUG=1: the 300-frame counters printed from es3_end_3d
     * and the debug-only glGetError checks. Read once at creation; the
     * getenv used to sit in the per-frame path. */
    bool debug;
    /*
     * The performance levers, each read once from the environment at
     * creation (ToriRS_ES3_New): NAME=0 selects the previous behaviour,
     * anything else (or unset) the new one. They exist so the two arms can
     * be A/B'd on the device with one binary; the control arms are not
     * deprecated code, they are the reference the new arms are judged
     * against.
     */

    int width;
    int height;
    int drawable_width;
    int drawable_height;
    /* Where frame pixels go inside the current target, GL origin: the
     * output rect when drawing direct, the whole buffer when offscreen. */
    int letterbox_x;
    int letterbox_y;
    /* letterbox_y with a top-left origin, for the maths that maps canvas rows
     * down the target and flips once at the end (scissors, world viewport). */
    int letterbox_top;
    int letterbox_width;
    int letterbox_height;
    /* --- client scaling (platform/client_scale.h) ----------------------- */
    struct ClientScaleSettings client_scale;
    /* The frame's rect on the drawable, GL origin (bottom-left). */
    int output_x;
    int output_y;
    int output_width;
    int output_height;
    /* The buffer this frame draws into: the drawable, or scale_fbo. */
    int target_width;
    int target_height;
    /* The render size differs from the output size: the frame is drawn into
     * scale_fbo and sampled onto the output rect at frame end. */
    bool target_offscreen;
    GLuint scale_fbo;
    GLuint scale_texture;
    GLuint scale_depth; /* 0 on the painter lane */
    int scale_fbo_width;
    int scale_fbo_height;
    GLint scale_texture_filter;
    /* Six UI-layout vertices covering clip space, and the program that
     * samples scale_texture through them. */
    GLuint present_vbo;
    struct ES3Program program_present;
    /* All Settings' interface scaling mode: 0 nearest, 1 linear, 2 bicubic.
     * @see es3_ui_layer_wanted. */
    int interface_scale_mode;
    /*
     * The interface layer. With a Linear or Bicubic interface filter and an
     * interface drawn at a size other than its layout size, each 2D segment
     * draws 1:1 into this layout-sized, premultiplied-alpha target and END_2D
     * filters the finished picture onto the output rect once
     * (es3_ui_layer_composite). Filtering every sprite and glyph on its own
     * instead bled neighbouring atlas cells into each other, showed every tile
     * boundary as a seam, and was never bicubic at all. Interface art itself
     * is always sampled nearest.
     *
     * While the layer is open the letterbox and target fields below describe
     * the layer (origin 0, layout size), so every scissor and viewport the 2D
     * stack computes lands 1:1 in it; the saved_* fields hold the frame's own
     * values until the composite restores them.
     */
    GLuint ui_layer_fbo;
    GLuint ui_layer_texture;
    int ui_layer_width;
    int ui_layer_height;
    bool ui_layer_open;
    GLuint ui_layer_saved_fbo;
    int ui_layer_saved_letterbox_x;
    int ui_layer_saved_letterbox_y;
    int ui_layer_saved_letterbox_top;
    int ui_layer_saved_letterbox_width;
    int ui_layer_saved_letterbox_height;
    int ui_layer_saved_target_width;
    int ui_layer_saved_target_height;
    struct ES3Program program_ui_composite;
    GLint ui_composite_u_size;
    GLint ui_composite_u_filter;

    /* --- programs and cached GL state ----------------------------------- */
    struct ES3Program program_world_plain;
    struct ES3Program program_world_cutout;
    struct ES3Program program_world_fast_plain;
    struct ES3Program program_world_fast_cutout;
    struct ES3Program program_ui;
    struct ES3Program program_rotmask;
    const struct ES3Program* current_program;
    GLuint bound_texture0;
    GLuint bound_texture1;
    GLuint bound_array_buffer;
    /*
     * Vertex array objects. One per world binding (its buffer never changes
     * once made, and the attribute offsets are fixed at zero now that an
     * index reaches the whole buffer) and one per 2D layout over this
     * frame's UI stream buffer. `vao_bound` is the state cache: a draw that
     * changes stream is one glBindVertexArray, not four
     * glVertexAttribPointer calls -- four JavaScript round trips in a
     * browser -- and the ES2 renderer issues those hundreds of times a
     * frame.
     *
     * The UI ones are keyed by the buffer they were built against because
     * the 2D stream rotates through ES3_FRAMES_IN_FLIGHT buffers; a VAO
     * whose buffer is not this frame's is re-specified, which happens once
     * per layout per frame at worst.
     */
    GLuint vao_world[ES3_BINDING_COUNT];
    /* What each world VAO was last specified against: the buffer object and
     * the byte offset of the binding's base vertex. A per-frame stream
     * landing somewhere new is the only thing that moves the second. */
    GLuint vao_world_buffer[ES3_BINDING_COUNT];
    uint32_t vao_world_base[ES3_BINDING_COUNT];
    /**
     * The element buffer each world VAO already has attached.
     *
     * Per VAO and not one `bound_element_buffer`, because the attachment IS
     * VAO state: binding a VAO restores whatever element buffer that VAO
     * carries, so a single context-wide cache has to be invalidated on every
     * VAO switch -- and then re-attached on the next draw. In painter order
     * the bindings alternate constantly (a depth-sorted frame interleaves
     * world chunks with the actor stream), so that cost is paid per draw
     * item. Remembering it per VAO makes the attachment happen once per
     * binding per frame, which is the thing a VAO is for. @see
     * es3_sequence_issue.
     */
    GLuint vao_element_buffer[ES3_BINDING_COUNT];
    /** TORIRS_ES3_TRIPLET_NEON: vst3q_u32 index triplets, four faces a step.
     *  On unless the knob says 0, so the scalar loop stays A/B-able.
     *  @see es3_painter_write_indices_ex. */
    /** TORIRS_ES3_DRAW_AUDIT: report any frame whose draw sequence lost an
     *  item or raised a GL error. Off by default; one line per bad frame. */
    bool draw_audit;
    /** Which route each static model took this frame, for the painter
     *  readout: the batch page (baked once, one VBO), the pose table (baked
     *  once into the arena), or a fresh arena bake. A non-trivial third
     *  number means static geometry is being re-baked per frame. */
    double stat_static_page;
    double stat_static_pose_table;
    double stat_static_rebake;
    GLuint vao_ui;
    GLuint vao_ui_buffer;
    uint32_t vao_ui_offset;
    GLuint vao_rotmask;
    GLuint vao_rotmask_buffer;
    uint32_t vao_rotmask_offset;
    GLuint vao_present;
    GLuint vao_bound;
    /*
     * The world pass's std140 uniform block: the matrix and the texture
     * clock, uploaded once per pass and read by all four world programs.
     * The ES2 renderer sends the matrix again for every program it switches
     * to, because a uniform belongs to a program object there.
     */
    GLuint world_ubo;
    /* The 2D stream buffer the VAOs above were specified against, and
     * whether the layout in use is the world's, the UI's or the rotmask's.
     * Kept for the debug counter and for the one place the 2D stack has to
     * know it re-pointed (a ring wrap mid-pass). */
    GLuint stream_buffer;
    uint32_t stream_byte_offset;
    int stream_layout;
    bool blend_on;
    bool depth_test_on;
    bool depth_write_on;
    bool cull_on;
    bool scissor_on;
    struct ES3Rect scissor_rect;

    /* --- world textures ------------------------------------------------- */
    struct TRSPK_Atlas atlas;
    GLuint atlas_texture;
    bool atlas_texture_allocated;
    int tex_slot_of_id[TORIDRAW_TEXTURE_ID_CAPACITY];
    uint8_t tex_resident[ES3_ATLAS_SLOTS];
    uint32_t tex_slot_next;
    /* Scratch for an upload whose SOURCE is not a straight sub-rectangle of
     * one buffer (an ARGB->RGBA swizzle, a glyph expansion). A plain
     * sub-rectangle needs none: GL_UNPACK_ROW_LENGTH hands the wider buffer
     * to glTexSubImage2D in place. */
    uint8_t* upload_stage;
    size_t upload_stage_capacity;

    /* --- retained geometry ---------------------------------------------- */
    struct ES3ModelGroup groups[TRSPK_VBO_GROUP_COUNT];
    /* Which buffer of every stream set this frame writes. */
    uint32_t frame_slot;
    struct ES3StreamSet index_stream;
    struct ES3StreamSet dynamic_stream;
    struct ES3StreamSet frame_stream;
    struct ES3StreamSet ui_stream;
    /* This frame's index buffer and the frame's staged indices. U32: an
     * index addresses its binding's whole buffer (see the header note). */
    GLuint ibo;
    uint32_t* ibo_staging;
    uint32_t ibo_staging_count;
    uint32_t ibo_staging_capacity;
    /* The draw sequence. */
    struct ES3DrawItem* draw_items;
    uint32_t draw_item_count;
    uint32_t draw_item_capacity;
    /*
     * The per-frame ordered vertex stream.
     *
     * Only the geometry that has no retained home goes here: an actor, whose
     * pose is this frame's and is baked straight in, in sorted face order.
     * Everything retained -- terrain, locs, every static pose -- is INDEXED
     * out of the buffer it was baked into, which is what 32-bit indices buy
     * (the GLES2 renderer copies all of it into this stream every frame
     * because its indices cannot reach past a 65,536-vertex page).
     */
    GLuint frame_stream_vbo;
    uint32_t frame_stream_gpu_base;
    struct TRSPK_VBO* frame_stream_cpu;
    struct TRSPK_Triangles frame_stream_triangles;
    uint32_t frame_stream_count;
    /* TORIRS_ES3_DEBUG=1: what the painter did, summed over 300 frames and
     * printed from es3_end_3d -- the numbers a CPU profile cannot give. */
    uint32_t painter_stat_frames;
    /* Retained faces drawn as indices into the buffer they were baked in,
     * and actor faces baked into this frame's stream. There is no third
     * number: nothing is gathered, which is the difference this renderer
     * exists for. */
    uint32_t painter_stat_faces_indexed;
    uint32_t painter_stat_faces_actor;
    uint32_t painter_stat_draws;
    /* The face sort's workload: models by bake size (2 faces = a terrain
     * tile, then <=16, <=64, <=256, larger), faces handed to the sort, and
     * faces that came out of it. The numbers that say where the sort's time
     * goes -- fixed per-model cost or per-face work. */
    uint32_t painter_stat_sort_models[5];
    uint32_t painter_stat_sort_faces_in;
    uint32_t painter_stat_sort_faces_out;
    /* The UI's draw calls and why each batch ended: a state change the
     * batch could not absorb (texture, atlas, scissor), the vertex cap, or a
     * flush a draw path asked for outright (the difference between draws
     * and the reasons counted). The driver's per-draw cost is the number
     * these decide. */
    uint32_t ui_stat_draws_batch;
    uint32_t ui_stat_draws_rotmask;
    uint32_t ui_stat_draws_widget;
    uint32_t ui_stat_break_texture;
    uint32_t ui_stat_break_atlas;
    uint32_t ui_stat_break_scissor;
    uint32_t ui_stat_break_overflow;
    uint32_t ui_stat_upload_bytes;
    struct TRSPK_PoseTable poses;
    struct TRSPK_PoseTable batch_poses;

    /* Batch16 is retained build-time storage. Its logical pages live in one
     * GL buffer sized in whole pages; a pose encodes a stable page id plus its
     * page-local vertex base. */
    struct ES3StaticBatch* static_batches;
    uint32_t static_batch_count;
    uint32_t static_batch_capacity;
    struct ES3StaticPageRef* static_pages;
    uint32_t static_page_count;
    uint32_t static_page_capacity;
    GLuint static_batch_vbo;
    /* In vertices: the buffer's size, and the bump allocator's high-water
     * mark over it (see struct ES3StaticPageRef). */
    uint32_t static_batch_gpu_vertex_capacity;
    uint32_t static_batch_gpu_vertex_used;
    int current_batch_slot;
    bool static_batch_upload_pending;

    /* Reused scratch makes each model one U16-chain append. */
    uint32_t* model_indices;
    uint32_t model_index_capacity;

    float view[16];
    float projection[16];
    float model_view_projection[16];
    struct ToriRS_RenderCommand_Begin3D current_3d;
    struct ES3Rect world_viewport;
    bool has_3d;
    bool in3d;
    double frame_clock;

    /* --- the 2D stack --------------------------------------------------- */
    bool in2d;
    float projection_2d[16];
    struct TRSPK_Atlas ui_sprite_atlas;
    GLuint ui_sprite_atlas_texture;
    bool ui_sprite_atlas_allocated;
    struct ES3UISpriteSlot ui_sprite_slots[ES3_UI_SPRITE_CAP];
    struct ES3UISpriteVariant ui_variants[ES3_UI_VARIANT_CAP];
    struct ES3UIFontSlot ui_fonts[ES3_UI_FONT_CAP];
    struct ES3UIRotmaskSlot* ui_rotmasks;
    uint32_t ui_rotmask_count;
    uint32_t ui_rotmask_capacity;
    struct ES3UIBatch ui_batch;
    /*
     * The deferred 2D pass (TORIRS_ES3_UI_DEFER): every UI-layout vertex
     * of the pass in one array, the few rotmask-layout vertices in a side
     * array, and the draw records over them. Sent and drawn by
     * es3_ui_submit; see struct ES3UIDrawRecord.
     */
    struct ES3VertexUI* ui_pass_vertices;
    uint32_t ui_pass_vertex_count;
    uint32_t ui_pass_vertex_capacity;
    struct ES3VertexRotmask* ui_pass_rotmask_vertices;
    uint32_t ui_pass_rotmask_count;
    uint32_t ui_pass_rotmask_capacity;
    struct ES3UIDrawRecord* ui_pass_records;
    uint32_t ui_pass_record_count;
    uint32_t ui_pass_record_capacity;
    /* Whether program_ui / program_rotmask already hold projection_2d.
     * Uniform values are PROGRAM object state (ES 2.0 §2.10.4): they survive
     * glUseProgram switches, so the matrix only needs re-sending when
     * projection_2d itself changes (es3_update_letterbox) or the program
     * is re-created. Cleared there; the control arm ignores them and pushes
     * per flush as before. */
    bool ui_projection_pushed;
    bool rotmask_projection_pushed;
    GLuint white_texture;
    /** This frame's 2D stream buffer (ui_stream's current one). */
    GLuint ui_vbo;
    /* Polygon run state; a run spans several commands by design. */
    struct ToriRS_RenderCommand_PolygonBegin polygon;
    int polygon_open;
    int polygon_x[TORIRS_POLYGON_MAX_POINTS];
    int polygon_y[TORIRS_POLYGON_MAX_POINTS];
    int polygon_count;
    /* Transient triangles for one widget model. */
    struct ES3VertexUI* widget_vertices;
    uint32_t widget_vertex_capacity;

    /* --- picking ---------------------------------------------------------- */
    bool pick_enabled;
    int pick_mouse_x;
    int pick_mouse_y;
    struct ToriRS_PickHits pick_hits;

    /* Resolved primary static poses, rebuilt with batch_poses. Draw-owned. */
    struct ES3StaticPrimary* static_primary;
    uint32_t static_primary_capacity;
    uint32_t* static_primary_bits;
    uint32_t static_resource_epoch;
    bool poses_prepared;
    bool actor_world_cache_enabled;
    float* actor_world_xyz;
    uint32_t actor_world_capacity;
};

/**
 * Where the shared es3_draw_model preamble placed one model's baked
 * vertices before handing it to a world path for emission.
 *
 * ::binding selects the vertex stream. ::absolute_base is the model's first
 * vertex IN THAT BINDING'S BUFFER -- what an array draw wants, and, because
 * every index here is 32-bit, also what an index is written against.
 * ::chunk_base is where the model's CPU source begins (a Batch16 chunk's
 * copy starts at zero, an arena's is the whole buffer), which is a different
 * number from ::absolute_base only for the static-page binding. ::page_id
 * names the static page (ES3_STATIC_PAGE_BINDING only; UINT32_MAX
 * otherwise) for whoever needs the chunk's CPU copy.
 */
struct ES3ModelPlacement
{
    uint32_t binding;
    uint32_t page_id;
    /** The Batch16 entry behind a static-page placement (UINT32_MAX when the
     *  model is not a batch entry): what the painter keys residency on. */
    uint32_t batch_slot;
    uint32_t entry_index;
    /** The entry's own baked vertex count -- the model's whole bake, which
     *  is NOT ::face_count * 3 on the painter path (there face_count is the
     *  post-cull sorted count). Zero when the model is not a batch entry. */
    uint32_t entry_vertex_count;
    uint32_t chunk_base;
    uint32_t absolute_base;
    int face_count;
    /* Faces left ordered in ToriDraw_FaceOrder by es3_painter_sort_faces.
     * Zero on the depth path, which does not sort here. */
    int sorted_face_count;
    int anim_index;
    int pose_id;
    bool dynamic;
    /**
     * The faces to draw, back to front, `sorted_face_count` of them. Where
     * the stage ran on this thread it is ToriDraw_FaceOrder of the scene;
     * where a stage source supplied it (the dual-core lane) it points into
     * that source's own storage. NULL on the depth path when the model was
     * not sorted -- the emit sorts it there if the material says it must.
     */
    const int* face_order;
    /** Whether the scene's own bench holds THIS model's projection right now.
     *  False when a stage source projected it elsewhere: a depth-path emit
     *  that needs to sort must project again first. */
    bool projected_in_scene;
    /** The model's projected depth (scene->projected_vertex.z after its
     *  projection), the blended-submission sort key. */
    int projected_depth;
};

/* ---- the two world implementations ------------------------------------- */

/* Fix up the projection trspk_compute_pass_matrices produced. */
void
es3_painter_setup_projection(struct ToriRS_ES3* renderer);
void
es3_zbuffer_setup_projection(
    struct ToriRS_ES3* renderer,
    const struct ToriRS_RenderCommand_Begin3D* command);

/* Depth-related state for the world pass. */
void
es3_painter_apply_world_states(struct ToriRS_ES3* renderer);

/** Send the frame's new residents to the window. Called by the core at the
 *  end of the pass, before the sequence draws. Safe with nothing staged. */
void
es3_painter_flush(struct ToriRS_ES3* renderer);

/** A batch was rebuilt, cleared or committed: forget every residency it had
 *  and make room for `entry_count` entries. */
void
es3_painter_batch_reset(
    struct ToriRS_ES3* renderer,
    struct ES3StaticBatch* batch,
    uint32_t entry_count);
void
es3_zbuffer_apply_world_states(struct ToriRS_ES3* renderer);

/* Order one model's faces up front (painter only). Return <= 0 to skip. */
int
es3_painter_sort_faces(
    struct ToriRS_ES3* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    int* out_sorted_face_count);

/* Append one model to the frame. */
void
es3_painter_emit_model(
    struct ToriRS_ES3* renderer,
    const struct ES3ModelPlacement* placement);
void
es3_zbuffer_emit_model(
    struct ToriRS_ES3* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct ES3ModelPlacement* placement);

/*
 * ToriRS_ES3_RenderFrame in three steps, for a lane that needs to do
 * something between them (the dual-core lane arms its worker after the
 * frame is begun and joins it before the frame is ended):
 *
 *   if( es3_render_frame_begin(renderer) )
 *   {
 *       ToriRS_FrameBegin(frame);
 *       es3_render_frame_commands(renderer, frame);
 *       ToriRS_FrameEnd(frame);
 *       es3_render_frame_end(renderer);
 *   }
 *
 * is exactly ToriRS_ES3_RenderFrame.
 */
bool
es3_render_frame_begin(struct ToriRS_ES3* renderer);
void
es3_render_frame_commands(struct ToriRS_ES3* renderer, struct ToriRS_Frame* frame);
void
es3_render_frame_end(struct ToriRS_ES3* renderer);

/*
 * The dispatch loop's per-command prefetch, for a lane that runs its own
 * loop: the element ids of the next three DRAW_MODEL commands (or -1),
 * nearest first. es3_render_frame_commands feeds it the frame's own
 * lookahead; the dual-core lane feeds it the commands sitting in its ring.
 * One line class per distance: the pose table's element row at +3, its
 * track's vertex-base array at +2, the static batch's entry at +1.
 */
void
es3_prefetch_ahead_ids(
    struct ToriRS_ES3* renderer,
    int id_plus1,
    int id_plus2,
    int id_plus3);

/* Depth-only entry points; the core calls them under a ::zbuffer test. */
bool
es3_zbuffer_create(struct ToriRS_ES3* renderer);
void
es3_zbuffer_destroy(struct ToriRS_ES3* renderer);

/** Clear the depth buffer, scissored to the world viewport. */
void
es3_zbuffer_begin_pass(struct ToriRS_ES3* renderer);

/** Depth/blend state for the opaque or the blended half of the sequence. */
void
es3_zbuffer_apply_pass_states(struct ToriRS_ES3* renderer, bool blended_pass);

/** Push the frame's opaque work onto the draw sequence: the coalesced array
 *  ranges first (the bulk of the static world), then the per-page index
 *  buckets. */
void
es3_zbuffer_flush_opaque(struct ToriRS_ES3* renderer);

/** Push the deferred blended submissions, back to front, after the opaque
 *  ones. */
void
es3_zbuffer_end_pass(struct ToriRS_ES3* renderer);

void
es3_zbuffer_report_memory(struct ToriRS_ES3* renderer);

/** Drop pass-scoped queues. Runs on every end-of-3D, including early exits. */
void
es3_zbuffer_reset_pass(struct ToriRS_ES3* renderer);

/* Retained-geometry notifications. The depth path caches a per-pose material
 * classification beside TRSPK's pose tables and uses these to stay in step. */
void
es3_zbuffer_pose_baked(
    struct ToriRS_ES3* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle);
void
es3_zbuffer_element_dropped(struct ToriRS_ES3* renderer, int element_id);
void
es3_zbuffer_track_dropped(
    struct ToriRS_ES3* renderer,
    int element_id,
    int anim_index);
void
es3_zbuffer_batch_pose_baked(
    struct ToriRS_ES3* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle);
void
es3_zbuffer_batch_dropped(
    struct ToriRS_ES3* renderer,
    struct TRSPK_Batch16* cpu);

/* ---- core services both world paths draw on ------------------------------ */

/** Grow renderer->model_indices to hold at least `needed` U32 indices. */
bool
es3_reserve_model_indices(struct ToriRS_ES3* renderer, uint32_t needed);

/* The draw sequence (see struct ES3DrawItem). */
void
es3_sequence_reset(struct ToriRS_ES3* renderer);
void
es3_sequence_push_indexed(
    struct ToriRS_ES3* renderer,
    uint32_t binding,
    uint32_t index_min,
    uint32_t index_max,
    bool cutout,
    bool blended,
    const uint32_t* indices,
    uint32_t index_count);
/* The two halves of es3_sequence_push_indexed for a caller that writes its
 * indices in place: reserve returns room for `index_count` indices at the
 * staging tail (the pointer is valid until the next reserve); commit records
 * the draw over exactly that many, merging with the open item as the push
 * does. Saves the copy: the painter writes ~1,000 index runs a frame. */
uint32_t*
es3_sequence_reserve_indexed(
    struct ToriRS_ES3* renderer,
    uint32_t index_count);
void
es3_sequence_commit_indexed(
    struct ToriRS_ES3* renderer,
    uint32_t binding,
    uint32_t index_min,
    uint32_t index_max,
    bool cutout,
    bool blended,
    uint32_t index_count);
void
es3_sequence_push_array(
    struct ToriRS_ES3* renderer,
    uint32_t binding,
    uint32_t first,
    uint32_t count,
    bool cutout,
    bool blended);
/** Upload the staged indices once and issue every item in order. */
void
es3_sequence_draw(struct ToriRS_ES3* renderer);

/** Reserve `vertex_count` vertices at the end of the frame stream; returns
 *  the first one's index. The CPU copy is renderer->frame_stream_cpu. */
uint32_t
es3_frame_stream_reserve(struct ToriRS_ES3* renderer, uint32_t vertex_count);

/** Push every dirty vertex buffer (groups and batch pages) to the GPU.
 *  False means the frame cannot draw the world. */
bool
es3_upload_geometry(struct ToriRS_ES3* renderer);

/** Bind `binding`'s world-layout vertex array object. No-op when already
 *  bound. False if the binding has no GPU buffer yet. */
bool
es3_bind_stream(struct ToriRS_ES3* renderer, uint32_t binding);

/** Bind an array buffer through the state cache (no-op when already bound;
 *  invalidates the attribute layout when it changes). */
void
es3_bind_array_buffer(struct ToriRS_ES3* renderer, GLuint buffer);

/** Drain glGetError, logging each with `where`; false when there was one. */
bool
es3_check_error(const char* where);

/** What the lane named this renderer, for the core's own log lines. */
char const*
es3_log_name(void);

/** The CPU side of a binding: its vertices and triangle configs. `page_id`
 *  names the chunk for ES3_STATIC_PAGE_BINDING and is ignored otherwise. */
bool
es3_binding_cpu_source(
    struct ToriRS_ES3* renderer,
    uint32_t binding,
    uint32_t page_id,
    const struct TRSPK_VBO** out_vbo,
    const struct TRSPK_Triangles** out_triangles);

/** Select a world program (plain or alpha-testing) and push its per-pass
 *  uniforms, then bind the atlas. */
/** Upload the world pass's shared uniform block (matrix + texture clock).
 *  Once per pass, before any world draw. */
void
es3_world_block_upload(struct ToriRS_ES3* renderer);

void
es3_use_world_program(struct ToriRS_ES3* renderer, bool cutout);

/* Cached GL state setters. */
void
es3_set_blend(struct ToriRS_ES3* renderer, bool enabled);
void
es3_set_depth(struct ToriRS_ES3* renderer, bool test, bool write);
void
es3_set_cull(struct ToriRS_ES3* renderer, bool enabled);
void
es3_set_scissor(struct ToriRS_ES3* renderer, const struct ES3Rect* rect);
void
es3_bind_texture0(struct ToriRS_ES3* renderer, GLuint texture);
void
es3_bind_texture1(struct ToriRS_ES3* renderer, GLuint texture);
void
es3_use_program(struct ToriRS_ES3* renderer, const struct ES3Program* program);

/* ---- what the UI unit implements for the core --------------------------- */

void
es3_ui_init_state(struct ToriRS_ES3* renderer);
bool
es3_ui_create_gl(struct ToriRS_ES3* renderer);
void
es3_ui_destroy_gl(struct ToriRS_ES3* renderer);
void
es3_ui_free(struct ToriRS_ES3* renderer);
void
es3_ui_report_memory(struct ToriRS_ES3* renderer);

void
es3_begin_2d(struct ToriRS_ES3* renderer);
void
es3_end_2d(struct ToriRS_ES3* renderer);
/** Every 2D draw recorded so far reaches the GPU: the open batch is closed
 *  and, on the deferred arm, the pass's records are uploaded and issued.
 *  Call before deleting or rewriting anything a recorded draw samples. */
void
es3_ui_flush(struct ToriRS_ES3* renderer);
void
es3_ui_batch_reset(struct ToriRS_ES3* renderer);

/** Immediate solid rectangle in logical coordinates, outside any 2D pass
 *  (the boot bar). */
void
es3_draw_solid_rect(
    struct ToriRS_ES3* renderer,
    int logical_x,
    int logical_y,
    int width,
    int height,
    uint32_t argb);

void
es3_ui_draw_sprite(
    struct ToriRS_ES3* renderer,
    const struct ToriRS_RenderCommand_Sprite* command);
void
es3_ui_draw_clear_rect(
    struct ToriRS_ES3* renderer,
    const struct ToriRS_RenderCommand_ClearRect* command);
void
es3_ui_draw_fill_rect(
    struct ToriRS_ES3* renderer,
    const struct ToriRS_RenderCommand_FillRect* command);
void
es3_ui_draw_line(
    struct ToriRS_ES3* renderer,
    const struct ToriRS_RenderCommand_Line* command);
void
es3_ui_draw_font(
    struct ToriRS_ES3* renderer,
    const struct ToriRS_RenderCommand_Font* command);
void
es3_ui_draw_model_widget(
    struct ToriRS_ES3* renderer,
    const struct ToriRS_RenderCommand_ModelWidget* command);
void
es3_ui_polygon_begin(
    struct ToriRS_ES3* renderer,
    const struct ToriRS_RenderCommand_PolygonBegin* command);
void
es3_ui_polygon_point(
    struct ToriRS_ES3* renderer,
    const struct ToriRS_RenderCommand_PolygonPoint* command);
void
es3_ui_polygon_end(struct ToriRS_ES3* renderer);
void
es3_ui_sprite_invalidate(struct ToriRS_ES3* renderer, int scene_id);
void
es3_ui_font_load(struct ToriRS_ES3* renderer, int font_id, struct ToriDraw_Font* font);
void
es3_ui_font_unload(struct ToriRS_ES3* renderer, int font_id);

/* ---- what the core exposes to the UI unit ------------------------------- */

/** Append `bytes` to this frame's 2D stream buffer; returns the byte offset
 *  they landed at, with renderer->ui_vbo left bound as the array buffer.
 *  `earlier_appends_drawn`: every byte appended to the 2D stream earlier
 *  this frame has already had its draw ISSUED (the immediate 2D path draws
 *  right after each append). Growth of the store may then orphan it; when
 *  false, growth is only allowed at offset 0 (see es3_stream_set_append). */
uint32_t
es3_ring_upload(
    struct ToriRS_ES3* renderer,
    const void* data,
    uint32_t bytes,
    bool earlier_appends_drawn);

/** Point the attributes at a UI-layout stream (the ring) at `byte_offset`;
 *  the rotmask variant adds the mask coordinate attribute. Both are no-ops
 *  when the attributes already point there. */
void
es3_bind_ui_stream(struct ToriRS_ES3* renderer, uint32_t byte_offset);
void
es3_bind_rotmask_stream(struct ToriRS_ES3* renderer, uint32_t byte_offset);

/** Grow renderer->upload_stage (the packed-row texture staging buffer) to at
 *  least `needed` bytes. Shared by the atlas and rotmask uploads. */
void
es3_reserve_upload_stage(struct ToriRS_ES3* renderer, size_t needed);

/** The rotmask source generation (ToriRS_ES3_RotmaskSourceChanged): the
 *  count of times the sprites behind a rotmask slot were rewritten in place. */
uint32_t
es3_rotmask_source_generation(void);

/** Convert a logical-space rectangle to a framebuffer scissor rectangle,
 *  clamped to the letterbox. False when nothing survives. */
bool
es3_scissor_rect(
    const struct ToriRS_ES3* renderer,
    int logical_x,
    int logical_y,
    int logical_width,
    int logical_height,
    struct ES3Rect* out);

/** Atlas services the widget path needs. */
int
es3_texture_slot(struct ToriRS_ES3* renderer, int tex_id);
int
es3_ensure_texture(struct ToriRS_ES3* renderer, int tex_id);
bool
es3_upload_atlas(struct ToriRS_ES3* renderer);
void
es3_map_atlas_uv(int slot, float local_u, float local_v, float* out_u, float* out_v);

/** The 2D (and world) blend function: straight-alpha "over" for colour,
 *  alpha accumulating as a + dst*(1-a). On the frame's own target the alpha
 *  channel is never shown; in the interface layer, cleared to transparent
 *  black, the pair leaves premultiplied colour and coverage behind, which is
 *  what makes the layer filterable without dark fringes. */
void
es3_blend_func_default(void);

/** Point the attributes at the clip-space present quad (six UI-layout
 *  vertices, v = 0 at the bottom), creating it on first use. */
void
es3_bind_present_quad(struct ToriRS_ES3* renderer);

/** RGBA bytes in memory order from a ToriDraw ARGB word. */
static inline uint32_t
es3_argb_to_rgba_bytes(uint32_t argb)
{
    return (argb & 0xff00ff00u) | ((argb >> 16) & 0xffu) | ((argb & 0xffu) << 16);
}

static inline int
es3_clampi(int value, int low, int high)
{
    if( value < low )
        return low;
    if( value > high )
        return high;
    return value;
}

#endif
