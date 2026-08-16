#ifndef TORIRS_PLATFORM_KIT_D3D8_FIXED_INTERNAL_H
#define TORIRS_PLATFORM_KIT_D3D8_FIXED_INTERNAL_H

#include "trspk_d3d8_fixed.h"

#include "../../tools/trspk_batch16.h"
#include "../../tools/trspk_resource_cache.h"
#include "../../tools/trspk_lru_model_cache.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d8.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

struct DashModel;
struct GGame;
struct ToriRSRenderCommand;
struct Platform2_Win32_Renderer_D3D8;

/* World-space vertex (3D pass). Matches TRSPK_VertexD3D8 byte-for-byte. */
struct D3D8WorldVertex
{
    float x, y, z;
    DWORD diffuse;
    float u, v;
};

struct D3D8UiVertex
{
    float x, y, z, rhw;
    DWORD diffuse;
    float u, v;
};

enum D3D8FixedTextureMode
{
    D3D8_FIXED_TEX_ATLAS  = 0, /* Default: TRSPK atlas + baked UVs, single MODULATE bind. */
    D3D8_FIXED_TEX_PER_ID = 1, /* Legacy: per-id IDirect3DTexture8, per-run SetTexture. */
};

enum PassKind
{
    PASS_NONE = 0,
    PASS_3D   = 1,
    PASS_2D   = 2,
};

/* One buffered sub-draw staged during event_draw_model, submitted at event_state_end_3d. */
struct D3D8FixedSubDraw
{
    IDirect3DVertexBuffer8* vbo;             /* per-model LRU VBO or batch chunk VBO */
    UINT                    vbo_offset_vertices; /* SetIndices BaseVertexIndex (vertex units) */
    uint16_t                world_idx;       /* index into D3D8FixedPassState::worlds */
    int                     tex_id;          /* atlas: ignored at submit; per-id: bound */
    bool                    opaque;          /* per-id only */
    float                   anim_signed;     /* per-id only */
    UINT                    pool_start_indices; /* first u16 entry in ib_scratch */
    UINT                    index_count;
};

struct D3D8FixedPassState
{
    std::vector<uint16_t>          ib_scratch; /* all face indices, sorted, for this frame pass */
    std::vector<D3D8FixedSubDraw>  subdraws;
    std::vector<D3DMATRIX>         worlds;     /* dedup by exact 16-float compare */
};

class D3D8FixedInternal
{
public:
    HMODULE            h_dll   = nullptr;
    IDirect3D8*        d3d     = nullptr;
    IDirect3DDevice8*  device  = nullptr;
    D3DPRESENT_PARAMETERS pp{};

    IDirect3DTexture8*    scene_rt_tex  = nullptr;
    IDirect3DSurface8*    scene_rt_surf = nullptr;
    IDirect3DSurface8*    scene_ds      = nullptr;
    IDirect3DIndexBuffer8* ib_ring      = nullptr;
    size_t ib_ring_size_bytes           = 0;

    UINT client_w = 0;
    UINT client_h = 0;

    /* --- TRSPK infrastructure ----------------------------------------- */
    TRSPK_ResourceCache* cache         = nullptr;
    TRSPK_Batch16*       batch_staging = nullptr;

    /* Atlas texture (world textures only; ATLAS mode). */
    IDirect3DTexture8* atlas_texture = nullptr;

    /* Batch chunk GPU buffers.  Overwritten on each BATCH3D_END; keyed by chunk_index. */
    IDirect3DVertexBuffer8* batch_chunk_vbos[TRSPK_MAX_WEBGL1_CHUNKS]{};
    IDirect3DIndexBuffer8*  batch_chunk_ebos[TRSPK_MAX_WEBGL1_CHUNKS]{};
    uint32_t                batch_chunk_count = 0u;

    /* Lazy GPU VBO map for LRU (per-instance) meshes.
     * Key: ((uint64_t)visual_id << 24) | ((uint64_t)seg << 16) | frame_index
     * Released on model unload. */
    std::unordered_map<uint64_t, IDirect3DVertexBuffer8*> lru_vbo_by_key;

    /* Per-id world texture maps (only populated when tex_mode == PER_ID). */
    std::unordered_map<int, IDirect3DTexture8*> texture_by_id;
    std::unordered_map<int, float>              texture_anim_speed_by_id;
    std::unordered_map<int, bool>               texture_opaque_by_id;

    /* Sprite + font maps (always per-id; atlas never covers 2D UI). */
    std::unordered_map<uint64_t, IDirect3DTexture8*>          sprite_by_slot;
    std::unordered_map<uint64_t, std::pair<int, int>>         sprite_size_by_slot;
    std::unordered_map<int, IDirect3DTexture8*>               font_atlas_by_id;

    /* Batch staging state. */
    uint32_t current_batch_id = 0;
    bool     batch_active     = false;

    /* Frame state. */
    D3D8FixedTextureMode tex_mode       = D3D8_FIXED_TEX_ATLAS;
    PassKind             current_pass   = PASS_NONE;
    int                  current_font_id = -1;
    double               frame_clock    = 0.0; /* game->cycle; baked into atlas UVs */

    unsigned int debug_model_draws = 0;
    unsigned int debug_triangles   = 0;

    size_t       ib_ring_write_offset = 0;
    unsigned int frame_count          = 0;
    bool         trace_first_gfx[32]{};

    D3DVIEWPORT8 frame_vp_3d{};
    D3DVIEWPORT8 frame_vp_2d{};

    D3DMATRIX frame_view{};
    D3DMATRIX frame_proj{};

    float frame_u_clock = 0.0f;
    int   frame_vp_w    = 0;
    int   frame_vp_h    = 0;

    std::vector<D3D8UiVertex> pending_font_verts;

    /* Buffered 3D pass — submitted at STATE_END_3D (or before a 2D op). */
    D3D8FixedPassState pass_state;

    void
    flush_font();

    void
    flush_3d_pass_if_needed();
};

D3D8FixedInternal*
trspk_d3d8_fixed_priv(TRSPK_D3D8FixedRenderer* r);

void
trspk_d3d8_fixed_log(const char* fmt, ...);

/* LRU VBO cache key: ((uint64_t)visual_id << 24) | ((uint64_t)(seg & 0xFF) << 16) | frame_index */
static inline uint64_t
d3d8_fixed_lru_vbo_key(uint16_t visual_id, uint8_t seg, uint16_t frame_index)
{
    return ((uint64_t)visual_id << 24) | ((uint64_t)(seg & 0xFFu) << 16) |
        (uint64_t)(frame_index & 0xFFFFu);
}

#endif
