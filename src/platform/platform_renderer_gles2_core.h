#ifndef SRC_PLATFORM_PLATFORM_RENDERER_GLES2_CORE_H
#define SRC_PLATFORM_PLATFORM_RENDERER_GLES2_CORE_H

/**
 * Shared surface between the GLES2 renderer's translation units.
 *
 *   platform_renderer_gles2_core.c     the context, the programs, the
 *                                              world texture atlas, every
 *                                              retained CPU/GPU vertex buffer,
 *                                              the per-frame index stream and
 *                                              the command dispatch
 *   platform_renderer_gles2_ui.c       the 2D stack: sprite atlas,
 *                                              fonts, rects, lines, polygons,
 *                                              rotated-masked chrome, widget
 *                                              models, the boot bar
 *   platform_renderer_gles2_painter.c  the painter's-algorithm world
 *                                              ordering; no depth buffer
 *   platform_renderer_gles2_zbuffer.c  the hardware depth ordering:
 *                                              a material pre-pass, array
 *                                              ranges for uniform poses, and
 *                                              a sorted blended pass
 *
 * The split and the contract are the D3D9 renderer's
 * (platform_win32_renderer_d3d9_core.h): ToriRS_GLES2_Init picks a world
 * implementation by creating (or not creating) the depth state, ::zbuffer is
 * that state and doubles as the selector, and the core calls gles2_painter_*
 * or gles2_zbuffer_* directly. The two world paths are peers; neither calls
 * the other.
 *
 * ## The GLES2 ceiling, and what each limit turned into
 *
 * This file and its .c files use OpenGL ES 2.0 core and NOTHING else -- no
 * extension is queried, let alone required. Concretely:
 *
 *   no 32-bit indices       every index is a uint16 and page-local. A vertex
 *                           buffer is addressed in 65,536-vertex PAGES; a
 *                           model never crosses one (the arena and Batch16
 *                           both guarantee it), and a draw's page is selected
 *                           by re-pointing the vertex attributes at the
 *                           page's byte offset. That is the same page model
 *                           D3D9 uses through BaseVertexIndex.
 *   no base-vertex draws    see above: the base IS the attribute pointer.
 *   no vertex array objects the attribute pointers are re-issued when the
 *                           (buffer, page) pair changes and not otherwise
 *                           (gles2_bind_stream tracks the last one).
 *   no buffer mapping       streams go through glBufferData orphaning and
 *                           glBufferSubData at a ring head that never
 *                           overtakes what the GPU is still reading.
 *   no UNPACK_ROW_LENGTH    a sub-rectangle of a CPU atlas is packed into a
 *                           tight staging buffer before glTexSubImage2D.
 *   no sized formats        GL_RGBA / GL_LUMINANCE_ALPHA / GL_ALPHA with
 *                           GL_UNSIGNED_BYTE, internalformat == format.
 *   no BGRA anything        ToriDraw ARGB is swizzled to RGBA bytes at
 *                           upload, and the vertex colour is stored in RGBA
 *                           byte order at bake.
 *   no uniform blocks       a handful of plain uniforms per program.
 *   NPOT textures           allowed in core ES2 only with CLAMP_TO_EDGE and
 *                           no mipmaps, which is how every NPOT texture here
 *                           (fonts, the rotmask sources) is created.
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

#include "platform/platform_renderer_gles2.h"

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

#include <GLES2/gl2.h>

#include <stdbool.h>
#include <stdint.h>

/* The world atlas: 16x16 tiles of 128, 256 slots, slot 0 an opaque white
 * tile for untextured faces. 2048 is the floor every GLES2 device this lane
 * runs on reports for GL_MAX_TEXTURE_SIZE; Init refuses a device below it. */
#define GLES2_ATLAS_DIM 2048u
#define GLES2_ATLAS_COLS 16u
#define GLES2_ATLAS_SLOTS 256u
/* One 16-bit index window. A slot never crosses a page boundary. */
#define GLES2_VBO_PAGE 65536u
#define GLES2_GPU_BUFFER_INIT 4096u
/*
 * Per-frame GPU streams are SETS of buffers, one per frame in flight, written
 * in rotation: a frame appends only into the buffer the GPU last read
 * GLES2_FRAMES_IN_FLIGHT frames ago, so no glBufferSubData ever lands on a
 * buffer with a draw outstanding. That matters more than it sounds on the
 * driver this lane runs on: an update to a busy buffer is "ghosted" -- the
 * driver allocates a fresh store and copies -- and a ring that was appended
 * every frame measured as ~400 page faults a frame, a quarter of the CPU, all
 * of it inside the driver's memcpy. A rotated set never triggers it, and a
 * buffer's pages stay resident from one use to the next.
 */
#define GLES2_FRAMES_IN_FLIGHT 4u
#define GLES2_INDEX_STREAM_INIT_BYTES (512u * 1024u)
#define GLES2_DYNAMIC_STREAM_INIT_BYTES (1024u * 1024u)
#define GLES2_DRAW_ITEM_INIT 1024u
/* How often a rotmask slot re-hashes its source sprite for a content change:
 * every Nth frame, staggered per slot. A minimap refresh lands at most N
 * frames late, and the hash -- a full read of the sprite -- costs 1/N. */
#define GLES2_ROTMASK_HASH_PERIOD 8u
#define GLES2_UI_ATLAS_DIM 2048u
#define GLES2_UI_SPRITE_CAP 2048
#define GLES2_UI_VARIANT_CAP 2048u
#define GLES2_UI_FONT_CAP 32
#define GLES2_UI_BATCH_MAX_VERTS 32768u
/* The 2D stream's opening size; a frame's UI is many small flushes. */
#define GLES2_UI_STREAM_INIT_BYTES (256u * 1024u)
#define GLES2_UI_ROTMASK_INIT_CAP 8u
#define GLES2_UI_FONT_BOX_MAX_LINES 64
#define GLES2_WIDGET_MODEL_NEAR 50.0f
#define GLES2_WORLD_NEAR 50.0f
#define GLES2_WORLD_FAR 32768.0f

#define GLES2_WIDGET_CONFIG_ATLAS (-1)
#define GLES2_WIDGET_CONFIG_NONE (-2)
#define GLES2_WIDGET_CONFIG_SKIP (-3)

/* Vertex stream bindings: the two arena groups, Batch16's page buffer, and
 * the painter path's per-frame ordered stream. */
#define GLES2_STATIC_PAGE_BINDING TRSPK_VBO_GROUP_COUNT
#define GLES2_FRAME_STREAM_BINDING (TRSPK_VBO_GROUP_COUNT + 1u)
/* The painter path's resident window (see platform_renderer_gles2_painter.c). */
#define GLES2_HOT_BINDING (TRSPK_VBO_GROUP_COUNT + 2u)
#define GLES2_BINDING_COUNT (TRSPK_VBO_GROUP_COUNT + 3u)
/* The painter path's frame stream: a ~40k-vertex world at 28 bytes. */
#define GLES2_FRAME_STREAM_INIT_BYTES (49152u * 28u)
/* A draw's index window is what a U16 index can reach from the bound
 * attribute pointer; the resident ring is several windows long, because a
 * frame's whole-bake working set (~88k vertices in Lumbridge) outgrows one.
 * A draw item's window slides: it opens at the address of the item's first
 * model, and later models join it while they lie within one window of it. */
#define GLES2_HOT_WINDOW_VERTICES 65536u
#define GLES2_HOT_RING_VERTICES (4u * GLES2_HOT_WINDOW_VERTICES)
/* World draw items in one frame above which the ring is judged fragmented
 * (placements scattered across windows by a long walk) and is emptied, so
 * the next frame re-places the live set contiguously in painter order. */
#define GLES2_HOT_COMPACT_DRAWS 48u
/* A batch pose locates its Batch16 ENTRY: the batch slot and the entry's
 * index in it, from which the chunk, the vertex base and the GPU page all
 * follow. An entry is the unit the painter keeps residency for. */
#define GLES2_BATCH_POSE_FLAG 0x80000000u
#define GLES2_BATCH_POSE_SLOT_SHIFT 24u
#define GLES2_BATCH_POSE_SLOT_MASK 0x7fu
#define GLES2_BATCH_POSE_ENTRY_MASK 0xffffffu
#define GLES2_BATCH_PAGE_LIMIT 32768u

/* Attribute locations, the same in every program. */
#define GLES2_ATTRIB_POSITION 0u
#define GLES2_ATTRIB_TEXCOORD 1u
#define GLES2_ATTRIB_COLOR 2u
#define GLES2_ATTRIB_TEXINFO 3u
#define GLES2_ATTRIB_MASK_TEXCOORD 3u

/*
 * The frame's draw sequence.
 *
 * One ordered list of draws, built by whichever world path is active and
 * issued once at the end of the pass. Two kinds of item share it:
 *
 *   indexed   a run of U16 page-local indices into one (binding, page); the
 *             indices are appended to one staging array and go to the GPU in
 *             a single ring upload;
 *   array     a contiguous run of vertices in a binding, drawn with
 *             glDrawArrays -- no indices at all. A dynamic model baked in its
 *             sorted face order is one of these; on the depth path so is every
 *             uniform opaque pose.
 *
 * Consecutive compatible items merge as they are pushed: two indexed items on
 * the same page and program become one draw, two array items that abut become
 * one draw. So the number of glDrawElements/glDrawArrays a frame issues is the
 * number of times the stream, the page or the program genuinely changed.
 */
struct GLES2DrawItem
{
    uint32_t binding;
    /** Indexed: the page's first vertex. Array: unused (0). */
    uint32_t page_base;
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
 * patch their anim bytes (gles2_refresh_texture_animation). */
#define GLES2_TRIANGLE_UNTEXTURED (-1)

/** A framebuffer-space rectangle: GL origin, y up. */
struct GLES2Rect
{
    int x;
    int y;
    int width;
    int height;
};

/* A clip in LOGICAL pixels, half-open. Axis-aligned UI quads are clipped
 * against it on the CPU (gles2_ui_append_quad_clipped), so no glScissor call
 * -- and no batch break -- stands between two quads with different clips. */
struct GLES2Clip
{
    int x0;
    int y0;
    int x1;
    int y1;
};

/** One per-frame GPU stream: GLES2_FRAMES_IN_FLIGHT buffers used in rotation,
 *  each appended from offset zero during its frame. */
struct GLES2StreamSet
{
    GLuint buffers[GLES2_FRAMES_IN_FLIGHT];
    uint32_t capacities[GLES2_FRAMES_IN_FLIGHT];
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
struct GLES2VertexUI
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
struct GLES2VertexRotmask
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

struct GLES2Program
{
    GLuint id;
    GLint u_matrix;
    GLint u_clock;
    GLint u_texture;
    GLint u_mask;
    GLint u_mask_invert;
};

/*
 * One scene sprite id's place in the UI atlas. `tiles` is kept across an
 * invalidate so a sprite REPLACED over a live id (the title flames, every
 * 35 ms) overwrites its own tile instead of consuming a fresh one until the
 * append-only packer is exhausted.
 */
struct GLES2UISpriteTile
{
    uint32_t x;
    uint32_t y;
    uint32_t w; /* padded, as inserted */
    uint32_t h;
    uint8_t valid;
};

struct GLES2UISpriteSlot
{
    int scene_id;
    int count;
    float* uvs;
    uint8_t* loaded;
    struct GLES2UISpriteTile* tiles;
};

struct GLES2UISpriteVariant
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

struct GLES2UIFontSlot
{
    int font_id;
    struct ToriDraw_Font* font;
    GLuint texture;
    int texture_width;
    int texture_height;
    float glyph_uv[TORIDRAW_FONT_GLYPH_COUNT * 4];
    bool baked;
};

struct GLES2UIRotmaskSlot
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
    bool used;
};

struct GLES2UIBatch
{
    struct GLES2VertexUI* vertices;
    uint32_t vertex_count;
    /* Unit 1's texture for the whole batch (0 until a quad names one); unit
     * 0 is always the sprite atlas. A quad naming a different unit-1 texture
     * is the one texture change that still ends a batch. */
    GLuint texture1;
    bool uses_sprite_atlas;
    bool scissor_enabled;
    struct GLES2Rect scissor;
};

struct GLES2StaticBatch
{
    int batch_id;
    struct TRSPK_Batch16* cpu;
    uint32_t* page_ids;
    uint32_t page_id_capacity;
    /* Per entry: the resident window serial it was placed at, 0 = never.
     * Owned by the painter path (gles2_painter_batch_reset). */
    uint32_t* hot_serial;
    uint32_t hot_serial_capacity;
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
struct GLES2StaticPageRef
{
    uint32_t batch_slot;
    uint32_t chunk_index;
    uint32_t gpu_offset;
    uint32_t gpu_capacity;
    bool valid;
};

/* The static page buffer starts at one window and doubles. */
#define GLES2_STATIC_BATCH_VBO_INIT_VERTICES 65536u

struct GLES2ModelGroup
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

struct GLES2ZBufferWorld;

struct ToriRS_GLES2
{
    struct ToriDraw_Scene* scene;
    /* Projection + face sort; the GPU table has no software raster stage. */
    const struct ToriDraw_Kernel* kernel;
    ToriRS_GLWindow* window;
    ToriRS_GLContext gl_context;

    /* The depth renderer's private state, and the mode selector: non-NULL
     * means the gles2_zbuffer_* implementation owns the world path. */
    struct GLES2ZBufferWorld* zbuffer;

    int width;
    int height;
    int drawable_width;
    int drawable_height;
    int letterbox_x;
    int letterbox_y;
    int letterbox_width;
    int letterbox_height;
    /* All Settings' interface scaling mode: 0 nearest, 1/2 linear. */
    int interface_scale_mode;
    bool ui_filter_dirty;

    /* --- programs and cached GL state ----------------------------------- */
    struct GLES2Program program_world_plain;
    struct GLES2Program program_world_cutout;
    struct GLES2Program program_ui;
    struct GLES2Program program_rotmask;
    const struct GLES2Program* current_program;
    GLuint bound_texture0;
    GLuint bound_texture1;
    GLuint bound_array_buffer;
    /* The vertex stream the attributes currently point into: which buffer,
     * which page, and whether it is the world layout or the UI one. */
    GLuint stream_buffer;
    uint32_t stream_byte_offset;
    int stream_layout;
    bool blend_on;
    bool depth_test_on;
    bool depth_write_on;
    bool cull_on;
    bool scissor_on;
    struct GLES2Rect scissor_rect;

    /* --- world textures ------------------------------------------------- */
    struct TRSPK_Atlas atlas;
    GLuint atlas_texture;
    bool atlas_texture_allocated;
    int tex_slot_of_id[TORIDRAW_TEXTURE_ID_CAPACITY];
    uint8_t tex_resident[GLES2_ATLAS_SLOTS];
    uint32_t tex_slot_next;
    /* Tightly packed rows for a sub-rectangle upload (no UNPACK_ROW_LENGTH). */
    uint8_t* upload_stage;
    size_t upload_stage_capacity;

    /* --- retained geometry ---------------------------------------------- */
    struct GLES2ModelGroup groups[TRSPK_VBO_GROUP_COUNT];
    /* Which buffer of every stream set this frame writes. */
    uint32_t frame_slot;
    struct GLES2StreamSet index_stream;
    struct GLES2StreamSet dynamic_stream;
    struct GLES2StreamSet frame_stream;
    struct GLES2StreamSet ui_stream;
    /* This frame's index buffer and the frame's staged indices. */
    GLuint ibo;
    uint16_t* ibo_staging;
    uint32_t ibo_staging_count;
    uint32_t ibo_staging_capacity;
    /* The draw sequence. */
    struct GLES2DrawItem* draw_items;
    uint32_t draw_item_count;
    uint32_t draw_item_capacity;
    /*
     * The painter path's frame stream.
     *
     * Painter order interleaves terrain, locs and actors tile by tile, which
     * under 16-bit page-local indexing is a page switch -- an attribute
     * re-point and a new draw call -- several hundred times a frame, and on
     * this class of GPU driver a draw call costs more than the geometry it
     * draws. So the painter path does not index at all: each model's SORTED
     * faces are copied out of its retained CPU bake (or, for an actor, baked
     * straight in) into one ordered vertex stream, the stream goes up once
     * through a ring, and the world is a single glDrawArrays. A frame's
     * world is ~40k vertices, about a megabyte -- a memcpy, not a cost.
     */
    GLuint frame_stream_vbo;
    uint32_t frame_stream_gpu_base;
    struct TRSPK_VBO* frame_stream_cpu;
    struct TRSPK_Triangles frame_stream_triangles;
    uint32_t frame_stream_count;
    /*
     * The painter path's resident window: a ring of GLES2_HOT_RING_VERTICES
     * on the GPU holding the static models being drawn. See
     * platform_renderer_gles2_painter.c. `hot_head` is the ring's
     * write serial (starts at one ring length, so serial 0 means "never
     * placed"); a model placed at serial s is resident while
     * hot_head - s <= ring. New residents are staged contiguously and go up
     * in one glBufferSubData per frame (gles2_painter_flush).
     */
    GLuint hot_vbo;
    uint32_t hot_head;
    /* The oldest serial among residents drawn THIS frame. A placement may
     * not advance the head past it plus one ring: that would overwrite the
     * bytes of a model this frame's draw has already been told to read, and
     * the model would flicker or draw garbage. Reset at the start of the
     * pass; a placement that cannot fit is refused and the model gathered. */
    uint32_t hot_frame_oldest_serial;
    struct TRSPK_VertexGLES2* hot_stage;
    uint32_t hot_stage_capacity;
    uint32_t hot_stage_count;
    uint32_t hot_stage_address;
    /* TORIRS_GLES2_DEBUG=1: what the painter did, summed over 300 frames and
     * printed from gles2_end_3d -- the numbers a CPU profile cannot give. */
    uint32_t painter_stat_frames;
    uint32_t painter_stat_faces_indexed;
    uint32_t painter_stat_faces_gathered;
    uint32_t painter_stat_faces_actor;
    uint32_t painter_stat_placed_vertices;
    uint32_t painter_stat_placed_models;
    uint32_t painter_stat_draws;
    uint32_t painter_stat_compactions;
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
    struct GLES2StaticBatch* static_batches;
    uint32_t static_batch_count;
    uint32_t static_batch_capacity;
    struct GLES2StaticPageRef* static_pages;
    uint32_t static_page_count;
    uint32_t static_page_capacity;
    GLuint static_batch_vbo;
    /* In vertices: the buffer's size, and the bump allocator's high-water
     * mark over it (see struct GLES2StaticPageRef). */
    uint32_t static_batch_gpu_vertex_capacity;
    uint32_t static_batch_gpu_vertex_used;
    int current_batch_slot;
    bool static_batch_upload_pending;

    /* Reused scratch makes each model one U16-chain append. */
    uint16_t* model_indices;
    uint32_t model_index_capacity;

    float view[16];
    float projection[16];
    float model_view_projection[16];
    struct ToriRS_RenderCommand_Begin3D current_3d;
    struct GLES2Rect world_viewport;
    bool has_3d;
    bool in3d;
    double frame_clock;

    /* --- the 2D stack --------------------------------------------------- */
    bool in2d;
    float projection_2d[16];
    struct TRSPK_Atlas ui_sprite_atlas;
    GLuint ui_sprite_atlas_texture;
    bool ui_sprite_atlas_allocated;
    struct GLES2UISpriteSlot ui_sprite_slots[GLES2_UI_SPRITE_CAP];
    struct GLES2UISpriteVariant ui_variants[GLES2_UI_VARIANT_CAP];
    struct GLES2UIFontSlot ui_fonts[GLES2_UI_FONT_CAP];
    struct GLES2UIRotmaskSlot* ui_rotmasks;
    uint32_t ui_rotmask_count;
    uint32_t ui_rotmask_capacity;
    struct GLES2UIBatch ui_batch;
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
    struct GLES2VertexUI* widget_vertices;
    uint32_t widget_vertex_capacity;

    /* --- picking ---------------------------------------------------------- */
    bool pick_enabled;
    int pick_mouse_x;
    int pick_mouse_y;
    struct ToriRS_PickHits pick_hits;
};

/**
 * Where the shared gles2_draw_model preamble placed one model's baked
 * vertices before handing it to a world path for emission.
 *
 * ::binding selects the vertex stream and ::page_base where the model's
 * chunk (or 64K arena page) begins in it, so ::local_base is chunk-local and
 * safe to widen into a U16 index against ::page_base. ::absolute_base is
 * page_base + local_base: what an array draw wants. ::page_id names the
 * static page (GLES2_STATIC_PAGE_BINDING only; UINT32_MAX otherwise) for
 * whoever needs the chunk's CPU copy.
 */
struct GLES2ModelPlacement
{
    uint32_t binding;
    uint32_t page_base;
    uint32_t page_id;
    /** The Batch16 entry behind a static-page placement (UINT32_MAX when the
     *  model is not a batch entry): what the painter keys residency on. */
    uint32_t batch_slot;
    uint32_t entry_index;
    /** The entry's own baked vertex count -- the model's whole bake, which
     *  is NOT ::face_count * 3 on the painter path (there face_count is the
     *  post-cull sorted count). Zero when the model is not a batch entry. */
    uint32_t entry_vertex_count;
    uint32_t local_base;
    uint32_t absolute_base;
    int face_count;
    /* Faces left ordered in ToriDraw_FaceOrder by gles2_painter_sort_faces.
     * Zero on the depth path, which does not sort here. */
    int sorted_face_count;
    int anim_index;
    int pose_id;
    bool dynamic;
};

/* The staging buffer for new residents starts here and doubles. */
#define GLES2_HOT_STAGE_INIT_VERTICES 4096u

/* ---- the two world implementations ------------------------------------- */

/* Fix up the projection trspk_compute_pass_matrices produced. */
void
gles2_painter_setup_projection(struct ToriRS_GLES2* renderer);
void
gles2_zbuffer_setup_projection(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_Begin3D* command);

/* Depth-related state for the world pass. */
void
gles2_painter_apply_world_states(struct ToriRS_GLES2* renderer);

/** Send the frame's new residents to the window. Called by the core at the
 *  end of the pass, before the sequence draws. Safe with nothing staged. */
void
gles2_painter_flush(struct ToriRS_GLES2* renderer);

/** A batch was rebuilt, cleared or committed: forget every residency it had
 *  and make room for `entry_count` entries. */
void
gles2_painter_batch_reset(
    struct ToriRS_GLES2* renderer,
    struct GLES2StaticBatch* batch,
    uint32_t entry_count);
void
gles2_zbuffer_apply_world_states(struct ToriRS_GLES2* renderer);

/* Order one model's faces up front (painter only). Return <= 0 to skip. */
int
gles2_painter_sort_faces(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    int* out_sorted_face_count);

/* Append one model to the frame. */
void
gles2_painter_emit_model(
    struct ToriRS_GLES2* renderer,
    const struct GLES2ModelPlacement* placement);
void
gles2_zbuffer_emit_model(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct GLES2ModelPlacement* placement);

/* Depth-only entry points; the core calls them under a ::zbuffer test. */
bool
gles2_zbuffer_create(struct ToriRS_GLES2* renderer);
void
gles2_zbuffer_destroy(struct ToriRS_GLES2* renderer);

/** Clear the depth buffer, scissored to the world viewport. */
void
gles2_zbuffer_begin_pass(struct ToriRS_GLES2* renderer);

/** Depth/blend state for the opaque or the blended half of the sequence. */
void
gles2_zbuffer_apply_pass_states(struct ToriRS_GLES2* renderer, bool blended_pass);

/** Push the frame's opaque work onto the draw sequence: the coalesced array
 *  ranges first (the bulk of the static world), then the per-page index
 *  buckets. */
void
gles2_zbuffer_flush_opaque(struct ToriRS_GLES2* renderer);

/** Push the deferred blended submissions, back to front, after the opaque
 *  ones. */
void
gles2_zbuffer_end_pass(struct ToriRS_GLES2* renderer);

void
gles2_zbuffer_report_memory(struct ToriRS_GLES2* renderer);

/** Drop pass-scoped queues. Runs on every end-of-3D, including early exits. */
void
gles2_zbuffer_reset_pass(struct ToriRS_GLES2* renderer);

/* Retained-geometry notifications. The depth path caches a per-pose material
 * classification beside TRSPK's pose tables and uses these to stay in step. */
void
gles2_zbuffer_pose_baked(
    struct ToriRS_GLES2* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle);
void
gles2_zbuffer_element_dropped(struct ToriRS_GLES2* renderer, int element_id);
void
gles2_zbuffer_track_dropped(
    struct ToriRS_GLES2* renderer,
    int element_id,
    int anim_index);
void
gles2_zbuffer_batch_pose_baked(
    struct ToriRS_GLES2* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle);
void
gles2_zbuffer_batch_dropped(
    struct ToriRS_GLES2* renderer,
    struct TRSPK_Batch16* cpu);

/* ---- core services both world paths draw on ------------------------------ */

/** Grow renderer->model_indices to hold at least `needed` U16 indices. */
bool
gles2_reserve_model_indices(struct ToriRS_GLES2* renderer, uint32_t needed);

/* The draw sequence (see struct GLES2DrawItem). */
void
gles2_sequence_reset(struct ToriRS_GLES2* renderer);
void
gles2_sequence_push_indexed(
    struct ToriRS_GLES2* renderer,
    uint32_t binding,
    uint32_t page_base,
    bool cutout,
    bool blended,
    const uint16_t* indices,
    uint32_t index_count);
/* The two halves of gles2_sequence_push_indexed for a caller that writes its
 * indices in place: reserve returns room for `index_count` indices at the
 * staging tail (the pointer is valid until the next reserve); commit records
 * the draw over exactly that many, merging with the open item as the push
 * does. Saves the copy: the painter writes ~1,000 index runs a frame. */
uint16_t*
gles2_sequence_reserve_indexed(
    struct ToriRS_GLES2* renderer,
    uint32_t index_count);
void
gles2_sequence_commit_indexed(
    struct ToriRS_GLES2* renderer,
    uint32_t binding,
    uint32_t page_base,
    bool cutout,
    bool blended,
    uint32_t index_count);
void
gles2_sequence_push_array(
    struct ToriRS_GLES2* renderer,
    uint32_t binding,
    uint32_t first,
    uint32_t count,
    bool cutout,
    bool blended);
/** Upload the staged indices once and issue every item in order. */
void
gles2_sequence_draw(struct ToriRS_GLES2* renderer);

/** Reserve `vertex_count` vertices at the end of the frame stream; returns
 *  the first one's index. The CPU copy is renderer->frame_stream_cpu. */
uint32_t
gles2_frame_stream_reserve(struct ToriRS_GLES2* renderer, uint32_t vertex_count);

/** Push every dirty vertex buffer (groups and batch pages) to the GPU.
 *  False means the frame cannot draw the world. */
bool
gles2_upload_geometry(struct ToriRS_GLES2* renderer);

/** Point the world attributes at `binding`'s buffer, page_base vertices in.
 *  No-op when already there. False if the binding has no GPU buffer yet. */
bool
gles2_bind_stream(struct ToriRS_GLES2* renderer, uint32_t binding, uint32_t page_base);

/** Bind an array buffer through the state cache (no-op when already bound;
 *  invalidates the attribute layout when it changes). */
void
gles2_bind_array_buffer(struct ToriRS_GLES2* renderer, GLuint buffer);

/** Drain glGetError, logging each with `where`; false when there was one. */
bool
gles2_check_error(const char* where);

/** The CPU side of a binding: its vertices and triangle configs. `page_id`
 *  names the chunk for GLES2_STATIC_PAGE_BINDING and is ignored otherwise. */
bool
gles2_binding_cpu_source(
    struct ToriRS_GLES2* renderer,
    uint32_t binding,
    uint32_t page_id,
    const struct TRSPK_VBO** out_vbo,
    const struct TRSPK_Triangles** out_triangles);

/** Select a world program (plain or alpha-testing) and push its per-pass
 *  uniforms, then bind the atlas. */
void
gles2_use_world_program(struct ToriRS_GLES2* renderer, bool cutout);

/* Cached GL state setters. */
void
gles2_set_blend(struct ToriRS_GLES2* renderer, bool enabled);
void
gles2_set_depth(struct ToriRS_GLES2* renderer, bool test, bool write);
void
gles2_set_cull(struct ToriRS_GLES2* renderer, bool enabled);
void
gles2_set_scissor(struct ToriRS_GLES2* renderer, const struct GLES2Rect* rect);
void
gles2_bind_texture0(struct ToriRS_GLES2* renderer, GLuint texture);
void
gles2_bind_texture1(struct ToriRS_GLES2* renderer, GLuint texture);
void
gles2_use_program(struct ToriRS_GLES2* renderer, const struct GLES2Program* program);

/* ---- what the UI unit implements for the core --------------------------- */

void
gles2_ui_init_state(struct ToriRS_GLES2* renderer);
bool
gles2_ui_create_gl(struct ToriRS_GLES2* renderer);
void
gles2_ui_destroy_gl(struct ToriRS_GLES2* renderer);
void
gles2_ui_free(struct ToriRS_GLES2* renderer);
void
gles2_ui_report_memory(struct ToriRS_GLES2* renderer);

void
gles2_begin_2d(struct ToriRS_GLES2* renderer);
void
gles2_end_2d(struct ToriRS_GLES2* renderer);
void
gles2_ui_flush(struct ToriRS_GLES2* renderer);
void
gles2_ui_batch_reset(struct ToriRS_GLES2* renderer);

/** Immediate solid rectangle in logical coordinates, outside any 2D pass
 *  (the boot bar). */
void
gles2_draw_solid_rect(
    struct ToriRS_GLES2* renderer,
    int logical_x,
    int logical_y,
    int width,
    int height,
    uint32_t argb);

void
gles2_ui_draw_sprite(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_Sprite* command);
void
gles2_ui_draw_clear_rect(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_ClearRect* command);
void
gles2_ui_draw_fill_rect(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_FillRect* command);
void
gles2_ui_draw_line(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_Line* command);
void
gles2_ui_draw_font(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_Font* command);
void
gles2_ui_draw_model_widget(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_ModelWidget* command);
void
gles2_ui_polygon_begin(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_PolygonBegin* command);
void
gles2_ui_polygon_point(
    struct ToriRS_GLES2* renderer,
    const struct ToriRS_RenderCommand_PolygonPoint* command);
void
gles2_ui_polygon_end(struct ToriRS_GLES2* renderer);
void
gles2_ui_sprite_invalidate(struct ToriRS_GLES2* renderer, int scene_id);
void
gles2_ui_font_load(struct ToriRS_GLES2* renderer, int font_id, struct ToriDraw_Font* font);
void
gles2_ui_font_unload(struct ToriRS_GLES2* renderer, int font_id);

/* ---- what the core exposes to the UI unit ------------------------------- */

/** Append `bytes` to this frame's 2D stream buffer; returns the byte offset
 *  they landed at, with renderer->ui_vbo left bound as the array buffer. */
uint32_t
gles2_ring_upload(struct ToriRS_GLES2* renderer, const void* data, uint32_t bytes);

/** Point the attributes at a UI-layout stream (the ring) at `byte_offset`;
 *  the rotmask variant adds the mask coordinate attribute. */
void
gles2_bind_ui_stream(struct ToriRS_GLES2* renderer, uint32_t byte_offset);
void
gles2_bind_rotmask_stream(struct ToriRS_GLES2* renderer, uint32_t byte_offset);

/** Convert a logical-space rectangle to a framebuffer scissor rectangle,
 *  clamped to the letterbox. False when nothing survives. */
bool
gles2_scissor_rect(
    const struct ToriRS_GLES2* renderer,
    int logical_x,
    int logical_y,
    int logical_width,
    int logical_height,
    struct GLES2Rect* out);

/** Atlas services the widget path needs. */
int
gles2_texture_slot(struct ToriRS_GLES2* renderer, int tex_id);
int
gles2_ensure_texture(struct ToriRS_GLES2* renderer, int tex_id);
bool
gles2_upload_atlas(struct ToriRS_GLES2* renderer);
void
gles2_map_atlas_uv(int slot, float local_u, float local_v, float* out_u, float* out_v);

GLenum
gles2_ui_filter(const struct ToriRS_GLES2* renderer);

/** RGBA bytes in memory order from a ToriDraw ARGB word. */
static inline uint32_t
gles2_argb_to_rgba_bytes(uint32_t argb)
{
    return (argb & 0xff00ff00u) | ((argb >> 16) & 0xffu) | ((argb & 0xffu) << 16);
}

static inline int
gles2_clampi(int value, int low, int high)
{
    if( value < low )
        return low;
    if( value > high )
        return high;
    return value;
}

#endif
