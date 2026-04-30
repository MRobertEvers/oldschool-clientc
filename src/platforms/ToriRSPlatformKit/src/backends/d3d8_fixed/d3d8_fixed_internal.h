#ifndef TORIRS_PLATFORM_KIT_D3D8_FIXED_INTERNAL_H
#define TORIRS_PLATFORM_KIT_D3D8_FIXED_INTERNAL_H

#include "trspk_d3d8_fixed.h"

#include "../../tools/trspk_facebuffer.h"

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

struct D3D8ModelGpu
{
    IDirect3DVertexBuffer8* vbo;
    int face_count;
    std::vector<int> per_face_raw_tex_id;
    D3D8WorldVertex first_face_verts[3];
    D3D8WorldVertex last_face_verts[3];
};

struct D3D8BatchModelEntry
{
    uint64_t model_key;
    int model_id;
    int vertex_base;
    int face_count;
    std::vector<int> per_face_raw_tex_id;
};

struct D3D8ModelBatch
{
    uint32_t batch_id = 0;
    IDirect3DVertexBuffer8* vbo = nullptr;
    IDirect3DIndexBuffer8* ebo = nullptr;
    int total_vertex_count = 0;
    std::vector<D3D8WorldVertex> pending_verts;
    std::vector<uint16_t> pending_indices;
    std::unordered_map<uint64_t, D3D8BatchModelEntry> entries_by_key;
};

enum PassKind
{
    PASS_NONE = 0,
    PASS_3D = 1,
    PASS_2D = 2,
};

class D3D8FixedInternal
{
public:
    HMODULE h_dll = nullptr;
    IDirect3D8* d3d = nullptr;
    IDirect3DDevice8* device = nullptr;
    D3DPRESENT_PARAMETERS pp{};

    IDirect3DTexture8* scene_rt_tex = nullptr;
    IDirect3DSurface8* scene_rt_surf = nullptr;
    IDirect3DSurface8* scene_ds = nullptr;
    IDirect3DIndexBuffer8* ib_ring = nullptr;
    size_t ib_ring_size_bytes = 0;

    UINT client_w = 0;
    UINT client_h = 0;

    std::unordered_map<int, IDirect3DTexture8*> texture_by_id;
    std::unordered_map<int, float> texture_anim_speed_by_id;
    std::unordered_map<int, bool> texture_opaque_by_id;

    std::unordered_map<uint64_t, D3D8ModelGpu*> model_gpu_by_key;

    std::unordered_map<uint64_t, IDirect3DTexture8*> sprite_by_slot;
    std::unordered_map<uint64_t, std::pair<int, int>> sprite_size_by_slot;

    std::unordered_map<int, IDirect3DTexture8*> font_atlas_by_id;

    PassKind current_pass = PASS_NONE;
    int current_font_id = -1;

    unsigned int debug_model_draws = 0;
    unsigned int debug_triangles = 0;

    size_t ib_ring_write_offset = 0;

    D3D8ModelBatch* current_batch = nullptr;
    std::unordered_map<uint32_t, D3D8ModelBatch*> batches_by_id;
    std::unordered_map<uint64_t, D3D8ModelBatch*> batched_model_batch_by_key;

    unsigned int frame_count = 0;
    bool trace_first_gfx[32]{};

    D3DVIEWPORT8 frame_vp_3d{};
    D3DVIEWPORT8 frame_vp_2d{};

    D3DMATRIX frame_view{};
    D3DMATRIX frame_proj{};

    float frame_u_clock = 0.0f;
    int frame_vp_w = 0;
    int frame_vp_h = 0;

    std::vector<D3D8UiVertex> pending_font_verts;

    TRSPK_FaceBuffer16 facebuffer{};

    void
    flush_font();
};

D3D8FixedInternal*
trspk_d3d8_fixed_priv(TRSPK_D3D8FixedRenderer* r);

void
trspk_d3d8_fixed_log(const char* fmt, ...);

#endif
