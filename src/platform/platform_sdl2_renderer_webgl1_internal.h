#ifndef SRC_PLATFORM_PLATFORM_SDL2_RENDERER_WEBGL1_INTERNAL_H
#define SRC_PLATFORM_PLATFORM_SDL2_RENDERER_WEBGL1_INTERNAL_H

/*
 * Shared innards of the WebGL1 backend.
 *
 * Private to platform_sdl2_renderer_webgl1.c (painter order) and
 * platform_sdl2_renderer_webgl1zb.c (depth buffered). The public interface is
 * platform_sdl2_renderer_gl3.h, which both GPU backends implement — the build
 * links exactly one of them (see platform/platform.mk), so the two never
 * coexist and the shared handle name is the interface, not the backend.
 *
 * ## Why this is not the GL3 header
 *
 * It used to be: one renderer compiled twice, with TORIRS_GL_ES2 choosing
 * between desktop GL and WebGL1 at 21 branches. That made WebGL1 correctness
 * depend on remembering to add an #else, and it stopped GL3 from using GL3 —
 * every desktop feature needed an ES2 twin beside it. The two are now separate
 * translation units with no shared preprocessor switch: this file may use only
 * WebGL1 with no extensions, and the GL3 one may use anything GL 3.2 offers.
 */

#include "platform/platform_sdl2_renderer_gl3.h"

#include "core/trspk_atlas.h"
#include "core/trspk_drawrangeex.h"
#include "core/trspk_drawrangelist.h"
#include "core/trspk_ibo.h"
#include "core/trspk_modelarena.h"
#include "core/trspk_pose.h"
#include "core/trspk_triangles.h"
#include "core/trspk_vbo.h"
#include "webgl1/trspk_webgl1.h"
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
 * when GL_MAX_TEXTURE_SIZE cannot take 4096 (see webgl1_init_texture_atlas). */
#define TRSPK_WEBGL1_ATLAS_DIM_PREF 4096u
#define TRSPK_WEBGL1_ATLAS_COLS_PREF 32u
#define TRSPK_WEBGL1_TEX_CAP_PREF 1024u
#define TRSPK_WEBGL1_ATLAS_DIM_FALLBACK 2048u
#define TRSPK_WEBGL1_ATLAS_COLS_FALLBACK 16u
#define TRSPK_WEBGL1_TEX_CAP_FALLBACK 256u
#define TRSPK_WEBGL1_TEX_CAP_MAX TRSPK_WEBGL1_TEX_CAP_PREF
#define TRSPK_WEBGL1_DRAWRANGE_CAP 4096u
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
#define TRSPK_WEBGL1_VBO_PAGE 65536u
/* Depth range for the z-buffered world pass, in world units. The near plane
 * follows the camera when it states one; this is the floor for it and the far
 * wall, matching D3D9's D3D9_WIDGET_MODEL_NEAR / D3D9_WORLD_FAR pair. */
#define TRSPK_WEBGL1_WORLD_NEAR 50.0f
#define TRSPK_WEBGL1_WORLD_FAR 32768.0f
#define TRSPK_WEBGL1_GPU_IBO_INIT 4096u
#define TRSPK_WEBGL1_GPU_VBO_INIT 4096u
#define TRSPK_WEBGL1_SPRITE_CAP 2048
#define TRSPK_WEBGL1_FONT_CAP 32
#define TRSPK_WEBGL1_2D_ATLAS_DIM 2048u
#define GL3_2D_BATCH_MAX_VERTS 32768u

/*
 * How a world face is drawn under the depth-buffered pass.
 *
 * The same four-way split D3D9 makes (WINDOWS-D3D9-ZBUFFER-001). The boundary
 * that matters is OPAQUE/CUTOUT versus BLENDED: the first two are
 * depth-order independent and can be drawn in natural face order with depth
 * writes on, while the third has to keep a back-to-front order and must not
 * write depth, or a translucent face would occlude whatever is behind it.
 *
 * CUTOUT is separate from OPAQUE because its texture has binary holes: it still
 * writes depth, but only for the texels the fragment shader keeps.
 */
enum WebGL1WorldFacePass
{
    WEBGL1_WORLD_FACE_SKIP = 0,
    WEBGL1_WORLD_FACE_OPAQUE = 1,
    WEBGL1_WORLD_FACE_CUTOUT = 2,
    WEBGL1_WORLD_FACE_BLENDED = 3,
};

/*
 * One model's translucent faces, held until the opaque pass is done.
 *
 * `depth` is the model's projected depth, which is what the pass sorts on —
 * per model, not per face. Faces within a model keep the priority/depth order
 * ToriDraw_RenderModel2SortFaces produced.
 */
/*
 * Cached face classification, keyed the way the geometry is: element -> track
 * -> pose.
 *
 * The classification depends on the pose's FINAL face alpha, which animation
 * writes when the pose is baked — so it is stable for as long as that baked
 * pose is, and recomputing it every frame is pure waste. D3D9 has cached it
 * beside the retained pose since it was written
 * (WINDOWS-D3D9-ZBUFFER-001, "It does not rescan model or texture pixels each
 * frame"); this is the same table.
 *
 * A pose whose entry is absent is classified on first use and stored. Unloading
 * an element or a track drops its entries, so a replaced animation cannot serve
 * a stale classification for a pose that no longer means the same thing.
 */
struct WebGL1MaterialPose
{
    uint8_t* face_passes;
    uint32_t face_count;
    uint32_t opaque_count;
    uint32_t cutout_count;
    uint32_t blended_count;
};

struct WebGL1MaterialTrack
{
    struct WebGL1MaterialPose* poses;
    uint32_t pose_count;
    uint32_t pose_capacity;
};

struct WebGL1MaterialElement
{
    struct WebGL1MaterialTrack tracks[TRSPK_POSE_TRACK_COUNT];
};

struct WebGL1MaterialTable
{
    struct WebGL1MaterialElement* elements;
    uint32_t element_count;
    uint32_t element_capacity;
};

struct WebGL1AlphaSubmission
{
    uint32_t group;
    uint32_t index_start;
    uint32_t index_count;
    int depth;
};

struct WebGL1SpriteSlot
{
    int scene_id;
    int count;
    float* uvs;
    uint8_t* loaded;
};

struct WebGL1SpriteVariant
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

struct WebGL1RotmaskDedicated
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

struct WebGL1FontSlot
{
    int font_id;
    struct ToriDraw_Font* font;
    GLuint texture;
    int atlas_w;
    int atlas_h;
    float glyph_uv[TORIDRAW_FONT_GLYPH_COUNT * 4];
    bool baked;
};

struct WebGL1Vertex2D
{
    float position[2];
    float texcoord[2];
    float color[4];
};

struct WebGL1Batch2DState
{
    struct WebGL1Vertex2D* verts;
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

struct WebGL1ModelGroup
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
    /*
     * Depth-buffered world pass instead of the painter one. The peer of D3D9's
     * --d3d9-zbuffer (WINDOWS-D3D9-ZBUFFER-001), decided at Init because it
     * changes the context's pixel format.
     */
    bool z_buffer_enabled;
    /* Per-model blended submissions, drawn after the opaque pass, sorted
     * back-to-front. Only models that actually have translucent faces land
     * here, so a scene without translucency leaves them empty. */
    /* Face classification cache for the depth pass; see the table above. */
    struct WebGL1MaterialTable materials;
    struct WebGL1AlphaSubmission* alpha_submissions;
    uint32_t alpha_submission_count;
    uint32_t alpha_submission_capacity;
    uint32_t* alpha_indices;
    uint32_t alpha_index_count;
    uint32_t alpha_index_capacity;
    uint32_t* alpha_order;
    uint32_t alpha_order_capacity;
    /* Scratch for one model's index list while it is being classified. */
    uint32_t* model_indices;
    uint32_t model_index_capacity;

    GLuint atlas_texture;
    /* Texture storage is allocated once; every later write is a dirty-rect
     * glTexSubImage2D. See webgl1_upload_atlas_texture. */
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
    uint8_t tex_resident[TRSPK_WEBGL1_TEX_CAP_MAX];

    struct WebGL1ModelGroup groups[TRSPK_VBO_GROUP_COUNT];
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
    /* 16-bit index staging, and the draw chunks it was split into. */
    uint16_t* idx16;
    uint32_t idx16_capacity;
    struct TRSPK_WebGL1Chunk* chunks;
    uint32_t chunk_capacity;

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
    struct WebGL1RotmaskDedicated rotmask_slots[GL3_ROTMASK_DEDICATED_CAP];
    GLuint rotmask_last_texture;
    struct WebGL1SpriteSlot sprite_slots[TRSPK_WEBGL1_SPRITE_CAP];
    struct WebGL1FontSlot font_slots[TRSPK_WEBGL1_FONT_CAP];
    GLuint quad_vao;
    GLuint quad_vbo;
    GLint u2d_projection;
    GLint u2d_texture;
    GLint u2d_text_mode;
    GLint u2d_uv_clamp;
    GLint u2d_uv_bounds;
    bool in2d;
    float proj2d[16];
    struct WebGL1Batch2DState batch2d;
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

/* --- shared helpers, implemented in platform_sdl2_renderer_webgl1.c ------- */

/** Bind a group's vertex buffer and attribute pointers, based at
 *  `base_vertex`. On WebGL1 the base IS the attribute state (no VAOs, no
 *  glDrawElementsBaseVertex); on desktop GL it binds the group's VAO. */
void
webgl1_bind_group_attribs(
    struct ToriRS_GL3* renderer,
    struct WebGL1ModelGroup* group,
    uint32_t base_vertex);

/** Grow the GPU element buffer to hold `index_count` indices. */
bool
webgl1_ensure_gpu_ibo(
    struct ToriRS_GL3* renderer,
    uint32_t index_count);

/** Grow the 16-bit index staging and the chunk table. */
bool
webgl1_ensure_index16(
    struct ToriRS_GL3* renderer,
    uint32_t index_count);

#endif
