#ifndef PLATFORM_IMPL2_SDL2_RENDERER_D3D11_H
#define PLATFORM_IMPL2_SDL2_RENDERER_D3D11_H

#include "platform_impl2_sdl2.h"
#include <SDL.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>

extern "C" {
#include "osrs/game.h"
#include "tori_rs.h"
}

struct D3D11FontAtlasEntry
{
    ID3D11Texture2D* texture;
    ID3D11ShaderResourceView* srv;
};

struct Platform2_SDL2_Renderer_D3D11
{
    ID3D11Device* device;
    ID3D11DeviceContext* device_context;
    IDXGISwapChain* swap_chain;
    ID3D11RenderTargetView* render_target_view;
    ID3D11DepthStencilView* depth_stencil_view;
    ID3D11Texture2D* depth_stencil_buffer;
    ID3D11DepthStencilState* depth_stencil_state;
    ID3D11RasterizerState* rasterizer_state;
    ID3D11BlendState* blend_state;
    ID3D11SamplerState* sampler_state;
    ID3D11Buffer* constant_buffer;

    // 3D world pass
    ID3D11VertexShader* world_vs;
    ID3D11PixelShader* world_ps;
    ID3D11InputLayout* world_input_layout;

    // 2D sprite pass
    ID3D11VertexShader* sprite_vs;
    ID3D11PixelShader* sprite_ps;
    ID3D11InputLayout* sprite_input_layout;

    // Font pass
    ID3D11VertexShader* font_vs;
    ID3D11PixelShader* font_ps;
    ID3D11InputLayout* font_input_layout;

    // 1x1 white dummy texture for untextured faces
    ID3D11Texture2D* dummy_texture;
    ID3D11ShaderResourceView* dummy_srv;

    struct Platform2_SDL2* platform;
    int width;
    int height;
    bool d3d11_ready;

    unsigned int debug_model_draws;
    unsigned int debug_triangles;

    int next_model_index;

    // Per-texture cached D3D11 resources
    std::unordered_map<int, ID3D11Texture2D*> texture_by_id;
    std::unordered_map<int, ID3D11ShaderResourceView*> texture_srv_by_id;

    // Match pix3dgl_load_texture: signed anim and opaque flag per texture id
    std::unordered_map<int, float> texture_anim_speed_by_id;
    std::unordered_map<int, bool> texture_opaque_by_id;

    // Index assignment for model keys
    std::unordered_map<uint64_t, int> model_index_by_key;

    // Stats / debug sets (mirrors the OpenGL3/Metal renderer)
    std::unordered_set<uint64_t> loaded_model_keys;
    std::unordered_set<uint64_t> loaded_scene_element_keys;
    std::unordered_set<int> loaded_texture_ids;

    // Sprite GPU texture cache keyed by DashSprite*
    std::unordered_map<void*, ID3D11Texture2D*> sprite_texture_by_ptr;
    std::unordered_map<void*, ID3D11ShaderResourceView*> sprite_srv_by_ptr;

    // Font atlas cache
    std::unordered_map<struct DashPixFont*, D3D11FontAtlasEntry> font_atlas_cache;
};

struct Platform2_SDL2_Renderer_D3D11*
PlatformImpl2_SDL2_Renderer_D3D11_New(
    int width,
    int height);

void
PlatformImpl2_SDL2_Renderer_D3D11_Free(
    struct Platform2_SDL2_Renderer_D3D11* renderer);

bool
PlatformImpl2_SDL2_Renderer_D3D11_Init(
    struct Platform2_SDL2_Renderer_D3D11* renderer,
    struct Platform2_SDL2* platform);

void
PlatformImpl2_SDL2_Renderer_D3D11_Render(
    struct Platform2_SDL2_Renderer_D3D11* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

#endif
