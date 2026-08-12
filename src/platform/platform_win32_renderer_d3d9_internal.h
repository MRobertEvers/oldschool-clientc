#ifndef SRC_PLATFORM_PLATFORM_WIN32_RENDERER_D3D9_INTERNAL_H
#define SRC_PLATFORM_PLATFORM_WIN32_RENDERER_D3D9_INTERNAL_H

/**
 * Shared surface between the D3D9 renderer core and its two world backends.
 *
 * The core (platform_win32_renderer_d3d9.c) owns the device, the 2D/UI stack,
 * textures and every retained CPU/GPU buffer.  It knows nothing about how the
 * world's triangles get ordered.  That is a world backend's job, and there are
 * two of them in separate translation units:
 *
 *   platform_win32_renderer_d3d9_world_painter.c   painter's algorithm; the
 *                                                  legacy RS ordering, no
 *                                                  depth buffer
 *   platform_win32_renderer_d3d9_world_zbuffer.c   hardware depth test, with a
 *                                                  material pre-pass and a
 *                                                  sorted blended pass
 *
 * ToriRS_D3D9_Init picks one from its z_buffer_enabled argument and the core
 * only ever reaches it through struct D3D9WorldBackend.  Nothing below the
 * vtable branches on the mode.
 */

#include "platform/platform_win32_renderer_d3d9.h"

#include "core/trspk_atlas.h"
#include "core/trspk_batch16.h"
#include "core/trspk_drawrangelist.h"
#include "core/trspk_ibo.h"
#include "core/trspk_modelarena.h"
#include "core/trspk_pose.h"
#include "core/trspk_triangles.h"
#include "core/trspk_vbo.h"
#include "render/torirs_frame.h"
#include "render/trspk_toridraw.h"

#include "toridraw_font.h"
#include "toridraw_model.h"
#include "toridraw_scene.h"
#include "toridraw_types.h"

#include <stdbool.h>
#include <stdint.h>

/* D3D9Ex is a Vista API.  Defining this before d3d9.h keeps even its types out
 * of every translation unit that includes this header and makes accidental use
 * a compile-time error. */
#ifndef D3D_DISABLE_9EX
#define D3D_DISABLE_9EX 1
#endif
#ifndef COBJMACROS
#define COBJMACROS 1
#endif
#include <windows.h>
#include <d3d9.h>

#define D3D9_ATLAS_DIM 2048u
#define D3D9_ATLAS_COLS 16u
#define D3D9_ATLAS_SLOTS 256u
#define D3D9_VBO_PAGE 65536u
#define D3D9_DRAWRANGE_CAP 4096u
#define D3D9_GPU_BUFFER_INIT 4096u
#define D3D9_UI_ATLAS_DIM 2048u
#define D3D9_UI_SPRITE_CAP 2048
#define D3D9_UI_VARIANT_CAP 2048u
#define D3D9_UI_FONT_CAP 32
#define D3D9_UI_BATCH_MAX_VERTS 32768u
#define D3D9_UI_ROTMASK_INIT_CAP 8u
#define D3D9_UI_FONT_BOX_MAX_LINES 64
#define D3D9_WIDGET_MODEL_NEAR 50.0f
#define D3D9_WORLD_FAR 65536.0f

#define D3D9_WIDGET_CONFIG_ATLAS (-1)
#define D3D9_WIDGET_CONFIG_NONE (-2)
#define D3D9_WIDGET_CONFIG_SKIP (-3)

#define D3D9_STATIC_PAGE_BINDING_BASE TRSPK_VBO_GROUP_COUNT
#define D3D9_BATCH_POSE_FLAG 0x80000000u
#define D3D9_BATCH_POSE_PAGE_MASK 0x7fffu
#define D3D9_BATCH_POSE_BASE_MASK 0xffffu
#define D3D9_BATCH_PAGE_LIMIT 32768u

#define D3D9_WORLD_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)
#define D3D9_OVERLAY_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)
#define D3D9_ROTMASK_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2)

struct D3D9OverlayVertex
{
    float x;
    float y;
    float z;
    float rhw;
    D3DCOLOR color;
    float u;
    float v;
};

struct D3D9RotmaskVertex
{
    float x;
    float y;
    float z;
    float rhw;
    D3DCOLOR color;
    float source_u;
    float source_v;
    float mask_u;
    float mask_v;
};

struct D3D9UISpriteSlot
{
    int scene_id;
    int count;
    float* uvs;
    uint8_t* loaded;
};

struct D3D9UISpriteVariant
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

struct D3D9UIFontSlot
{
    int font_id;
    struct ToriDraw_Font* font;
    IDirect3DTexture9* texture;
    UINT texture_width;
    UINT texture_height;
    int atlas_width;
    int atlas_height;
    float glyph_uv[TORIDRAW_FONT_GLYPH_COUNT * 4];
    bool baked;
};

struct D3D9UIRotmaskSlot
{
    int scene_id;
    int atlas_index;
    int mask_scene_id;
    int mask_atlas_index;
    int width;
    int height;
    int source_width;
    int source_height;
    IDirect3DTexture9* source_texture;
    UINT source_texture_width;
    UINT source_texture_height;
    IDirect3DTexture9* mask_texture;
    UINT mask_texture_width;
    UINT mask_texture_height;
    bool used;
};

struct D3D9UIBatch
{
    struct D3D9OverlayVertex* vertices;
    uint32_t vertex_count;
    IDirect3DTexture9* texture;
    bool uses_sprite_atlas;
    bool scissor_enabled;
    RECT scissor;
};

struct D3D9WidgetVertex
{
    float cx;
    float cy;
    float cz;
    float color[4];
    float u;
    float v;
};

struct D3D9StaticBatch
{
    int batch_id;
    struct TRSPK_Batch16* cpu;
    uint32_t* page_ids;
    uint32_t page_id_capacity;
    bool active;
    bool building;
};

struct D3D9StaticPageRef
{
    uint32_t batch_slot;
    uint32_t chunk_index;
    bool valid;
};

struct D3D9ModelGroup
{
    struct TRSPK_VBO* vbo_cpu;
    IDirect3DVertexBuffer9* vbo_gpu;
    uint32_t gpu_capacity;
    struct TRSPK_ModelArena* arena;
    struct TRSPK_Triangles triangles;
    bool reset_each_frame;
};

struct D3D9WorldBackend;

struct ToriRS_D3D9
{
    struct ToriDraw_Scene* scene;
    HWND hwnd;
    IDirect3D9* d3d;
    IDirect3DDevice9* device;
    D3DPRESENT_PARAMETERS present;
    D3DCAPS9 caps;
    bool reset_pending;
    bool scene_active;

    /* The world ordering strategy and its private per-mode state.  ::world is
     * never NULL once ToriRS_D3D9_New has returned; ::world_state belongs to
     * the backend and is opaque here. */
    const struct D3D9WorldBackend* world;
    void* world_state;

    int width;
    int height;
    int client_w;
    int client_h;
    int lb_x;
    int lb_y;
    int lb_w;
    int lb_h;

    struct TRSPK_Atlas atlas;
    IDirect3DTexture9* atlas_texture;
    int tex_slot_of_id[TORIDRAW_TEXTURE_ID_CAPACITY];
    uint8_t tex_resident[D3D9_ATLAS_SLOTS];
    uint32_t tex_slot_next;
    IDirect3DTexture9* animated_textures[TORIDRAW_TEXTURE_ID_CAPACITY];

    struct D3D9ModelGroup groups[TRSPK_VBO_GROUP_COUNT];
    IDirect3DIndexBuffer9* ibo;
    uint32_t gpu_ibo_capacity;
    struct TRSPK_PoseTable poses;
    struct TRSPK_PoseTable batch_poses;
    struct TRSPK_IBOChain* ibo_chain;
    struct TRSPK_DrawRangeList* draw_ranges;

    /* Batch16 is retained build-time storage. Its logical pages (at most
     * 65,535 live vertices in a 65,536-vertex stride) live in one managed D3D9
     * VBO; a pose encodes a stable page id plus its page-local vertex base.
     * Per-frame U16 indices select a page through
     * DrawIndexedPrimitive(BaseVertexIndex), so page changes never rebind the
     * vertex stream. */
    struct D3D9StaticBatch* static_batches;
    uint32_t static_batch_count;
    uint32_t static_batch_capacity;
    struct D3D9StaticPageRef* static_pages;
    uint32_t static_page_count;
    uint32_t static_page_capacity;
    IDirect3DVertexBuffer9* static_batch_vbo;
    uint32_t static_batch_gpu_page_capacity;
    int current_batch_slot;
    bool static_batch_upload_pending;

    /* Reused scratch makes each sorted model one U16-chain append. */
    uint16_t* model_indices;
    uint32_t model_index_capacity;

    float view[16];
    float proj[16];
    struct ToriRS_RenderCommand_Begin3D cur_3d;
    bool has_3d;
    bool in3d;
    double frame_clock;

    bool in2d;

    /* Retained native 2D resources. Static pixels enter these caches once and
     * normal frames only resubmit a compact vertex stream. */
    struct TRSPK_Atlas ui_sprite_atlas;
    IDirect3DTexture9* ui_sprite_atlas_texture;
    struct D3D9UISpriteSlot ui_sprite_slots[D3D9_UI_SPRITE_CAP];
    struct D3D9UISpriteVariant ui_variants[D3D9_UI_VARIANT_CAP];
    struct D3D9UIFontSlot ui_fonts[D3D9_UI_FONT_CAP];
    struct D3D9UIRotmaskSlot* ui_rotmasks;
    uint32_t ui_rotmask_count;
    uint32_t ui_rotmask_capacity;
    struct D3D9UIBatch ui_batch;
    uint64_t ui_texture_upload_bytes;
    uint64_t ui_texture_upload_count;

    bool pick_enabled;
    int pick_mouse_x;
    int pick_mouse_y;
    struct ToriRS_PickHits pick_hits;
};

/**
 * Where the shared d3d9_draw_model preamble placed one model's baked vertices
 * before handing it to the world backend for index emission.
 *
 * ::binding selects the vertex stream (a TRSPK_VBO group, or
 * D3D9_STATIC_PAGE_BINDING_BASE for Batch16's managed VBO) and ::page_base the
 * 64K page inside it, so ::local_base is already page-local and safe to widen
 * into a U16 index.
 */
struct D3D9ModelPlacement
{
    uint32_t binding;
    uint32_t page_base;
    uint32_t local_base;
    int face_count;
    /* Faces the backend's model_face_count left ordered in ToriDraw_FaceOrder.
     * Zero when the backend did not sort. */
    int sorted_face_count;
    int anim_index;
    int pose_id;
    bool dynamic;
};

/**
 * One world ordering strategy.  Every hook is mandatory -- a backend that has
 * nothing to do for one supplies an explicit no-op rather than leaving NULL,
 * so the core never has to null-check a call site.
 */
struct D3D9WorldBackend
{
    /* Names the mode in diagnostics. */
    const char* name;

    /* Whether ToriRS_D3D9_Init must ask D3D for an auto depth-stencil surface. */
    bool wants_depth_stencil;

    /* Allocate and release renderer->world_state.  create returns false only
     * on allocation failure. */
    bool (*create)(struct ToriRS_D3D9* renderer);
    void (*destroy)(struct ToriRS_D3D9* renderer);

    /* Start of a 3D pass, called once the D3D viewport for the pass is set so
     * that any Clear here is scissored to it. */
    void (*begin_pass)(struct ToriRS_D3D9* renderer);

    /* Fix up the projection trspk_compute_pass_matrices just produced.  The
     * two modes agree on X/Y scale and disagree only about clip Z. */
    void (*setup_projection)(
        struct ToriRS_D3D9* renderer,
        const struct ToriRS_RenderCommand_Begin3D* command);

    /* Flush mode-owned draw passes the shared opaque chain did not cover.
     * Runs after the core has drawn renderer->ibo_chain. */
    void (*end_pass)(struct ToriRS_D3D9* renderer);

    /* Drop pass-scoped queues.  Runs on every end-of-3D, including the early
     * exits that never drew anything. */
    void (*reset_pass)(struct ToriRS_D3D9* renderer);

    /* The depth-related render states, applied at the point in
     * d3d9_set_world_states where they used to be spelled inline. */
    void (*apply_world_states)(struct ToriRS_D3D9* renderer);

    /* Per-chain state for a single d3d9_draw_retained call. */
    void (*apply_pass_states)(struct ToriRS_D3D9* renderer, bool blended_pass);

    /* How many faces this model contributes.  A backend that orders faces up
     * front does so here and reports the count through out_sorted_face_count;
     * one that does not leaves it at zero.  Return <= 0 to skip the model. */
    int (*model_face_count)(
        struct ToriRS_D3D9* renderer,
        const struct ToriRS_RenderCommand_Model* command,
        int* out_sorted_face_count);

    /* Append this model's indices to the frame's chains. */
    void (*model_emit)(
        struct ToriRS_D3D9* renderer,
        const struct ToriRS_RenderCommand_Model* command,
        const struct D3D9ModelPlacement* placement);

    /* Retained-geometry notifications.  A mode that caches per-pose CPU data
     * alongside TRSPK's pose tables uses these to stay in step with them; a
     * mode that caches nothing supplies no-ops. */
    void (*pose_baked)(
        struct ToriRS_D3D9* renderer,
        int element_id,
        int anim_index,
        int pose_id,
        struct ToriDraw_ModelHandle handle);
    void (*element_dropped)(struct ToriRS_D3D9* renderer, int element_id);
    void (*track_dropped)(
        struct ToriRS_D3D9* renderer,
        int element_id,
        int anim_index);
    void (*batch_pose_baked)(
        struct ToriRS_D3D9* renderer,
        int element_id,
        int anim_index,
        int pose_id,
        struct ToriDraw_ModelHandle handle);
    void (*batch_dropped)(
        struct ToriRS_D3D9* renderer,
        struct TRSPK_Batch16* cpu);
};

/** The painter's-algorithm world backend. */
const struct D3D9WorldBackend*
d3d9_world_backend_painter(void);

/** The hardware depth-test world backend. */
const struct D3D9WorldBackend*
d3d9_world_backend_zbuffer(void);

/*
 * Core services the world backends draw on.  These stay owned by the core file;
 * only the handful a backend genuinely needs is exported here.
 */

/** Grow renderer->model_indices to hold at least `needed` U16 indices. */
bool
d3d9_reserve_model_indices(struct ToriRS_D3D9* renderer, uint32_t needed);

/** Upload and draw one index chain through the retained world pipeline. */
void
d3d9_draw_retained(
    struct ToriRS_D3D9* renderer,
    struct TRSPK_IBOChain* chain,
    bool blended_pass);

#endif
