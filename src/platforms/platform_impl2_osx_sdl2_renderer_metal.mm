// System ObjC/Metal headers must come before any game headers to avoid
// the rsbuf.h #define pwrite macro colliding with unistd.h's declaration.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "platform_impl2_osx_sdl2_renderer_metal.h"

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_sdl2.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "graphics/uv_pnm.h"

extern "C" {
#include "graphics/dash.h"
#include "graphics/shared_tables.h"
#include "osrs/game.h"
#include "tori_rs_render.h"
}

// Must match src/osrs/pix3dglcore.u.cpp (used by Pix3DGL / OpenGL3 renderer).
static void
metal_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw)
{
    float cosPitch = cosf(-pitch);
    float sinPitch = sinf(-pitch);
    float cosYaw   = cosf(-yaw);
    float sinYaw   = sinf(-yaw);

    out_matrix[0]  = cosYaw;
    out_matrix[1]  = sinYaw * sinPitch;
    out_matrix[2]  = sinYaw * cosPitch;
    out_matrix[3]  = 0.0f;

    out_matrix[4]  = 0.0f;
    out_matrix[5]  = cosPitch;
    out_matrix[6]  = -sinPitch;
    out_matrix[7]  = 0.0f;

    out_matrix[8]  = -sinYaw;
    out_matrix[9]  = cosYaw * sinPitch;
    out_matrix[10] = cosYaw * cosPitch;
    out_matrix[11] = 0.0f;

    out_matrix[12] = -camera_x * cosYaw + camera_z * sinYaw;
    out_matrix[13] =
        -camera_x * sinYaw * sinPitch - camera_y * cosPitch - camera_z * cosYaw * sinPitch;
    out_matrix[14] =
        -camera_x * sinYaw * cosPitch + camera_y * sinPitch - camera_z * cosYaw * cosPitch;
    out_matrix[15] = 1.0f;
}

static void
metal_compute_projection_matrix(float* out_matrix, float fov, float screen_width, float screen_height)
{
    float y = 1.0f / tanf(fov * 0.5f);
    float x = y;

    out_matrix[0]  = x * 512.0f / (screen_width / 2.0f);
    out_matrix[1]  = 0.0f;
    out_matrix[2]  = 0.0f;
    out_matrix[3]  = 0.0f;

    out_matrix[4]  = 0.0f;
    out_matrix[5]  = -y * 512.0f / (screen_height / 2.0f);
    out_matrix[6]  = 0.0f;
    out_matrix[7]  = 0.0f;

    out_matrix[8]  = 0.0f;
    out_matrix[9]  = 0.0f;
    out_matrix[10] = 0.0f;
    out_matrix[11] = 1.0f;

    out_matrix[12] = 0.0f;
    out_matrix[13] = 0.0f;
    out_matrix[14] = -1.0f;
    out_matrix[15] = 0.0f;
}

// Column-major 4x4 multiply: out = a * b
static void
mat4_mul_colmajor(const float* a, const float* b, float* out)
{
    for( int c = 0; c < 4; ++c )
        for( int r = 0; r < 4; ++r )
        {
            float s = 0.0f;
            for( int k = 0; k < 4; ++k )
                s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}

// OpenGL NDC z is in [-1, 1]; Metal requires z/w in [0, 1] after divide.
// clip_out.z = 0.5 * clip_in.z + 0.5 * clip_in.w; clip_out.w unchanged.
static void
metal_remap_projection_opengl_to_metal_z(float* proj_colmajor)
{
    static const float k_clip_z[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f,
    };
    float tmp[16];
    mat4_mul_colmajor(k_clip_z, proj_colmajor, tmp);
    memcpy(proj_colmajor, tmp, sizeof(tmp));
}

// ---------------------------------------------------------------------------
// Vertex layout that matches Shaders.metal (stride 64, 16-byte aligned)
// ---------------------------------------------------------------------------
struct MetalVertex
{
    float position[4]; // xyz + unused w (1.0)
    float color[4];    // r, g, b, a
    float texcoord[2];
    float texBlend;           // 1.0 = sample texture * color (Pix3DGL), 0.0 = color only
    float textureOpaque;      // 1.0 = opaque tex; 0.0 = alpha-tested like GL path
    float textureAnimSpeed;   // signed, same encoding as pix3dgl_load_texture
    float _pad_vertex;
};

// ---------------------------------------------------------------------------
// Uniform buffer that matches Shaders.metal
// ---------------------------------------------------------------------------
struct MetalUniforms
{
    float modelViewMatrix[16];
    float projectionMatrix[16];
    float uClock;
    float _pad_uniform[3];
};

// ---------------------------------------------------------------------------
// Viewport helpers (identical to opengl3 renderer)
// ---------------------------------------------------------------------------
struct MTLViewportRect
{
    int x, y, width, height;
};

struct LogicalViewportRect
{
    int x, y, width, height;
};

static LogicalViewportRect
compute_logical_viewport_rect(int window_width, int window_height, const struct GGame* game)
{
    LogicalViewportRect rect = { 0, 0, window_width, window_height };
    if( window_width <= 0 || window_height <= 0 || !game || !game->view_port )
        return rect;

    int x = game->viewport_offset_x;
    int y = game->viewport_offset_y;
    int w = game->view_port->width;
    int h = game->view_port->height;
    if( w <= 0 || h <= 0 )
        return rect;

    if( x < 0 ) x = 0;
    if( y < 0 ) y = 0;
    if( x >= window_width || y >= window_height )
        return rect;
    if( x + w > window_width )  w = window_width  - x;
    if( y + h > window_height ) h = window_height - y;
    if( w <= 0 || h <= 0 )
        return rect;

    rect.x = x; rect.y = y; rect.width = w; rect.height = h;
    return rect;
}

// Same as platform_impl2_osx_sdl2_renderer_opengl3.cpp: rect.y is OpenGL
// bottom-left Y (not top-left).
static MTLViewportRect
compute_gl_world_viewport_rect(
    int fb_width, int fb_height,
    int win_width, int win_height,
    const LogicalViewportRect& lr)
{
    MTLViewportRect rect = { 0, 0, fb_width, fb_height };
    if( fb_width <= 0 || fb_height <= 0 || win_width <= 0 || win_height <= 0 )
        return rect;

    const double sx = (double)fb_width  / (double)win_width;
    const double sy = (double)fb_height / (double)win_height;

    int scaled_x     = (int)lround((double)lr.x * sx);
    int scaled_top_y = (int)lround((double)lr.y * sy);
    int scaled_w     = (int)lround((double)lr.width * sx);
    int scaled_h     = (int)lround((double)lr.height * sy);

    int clamped_x = scaled_x < 0 ? 0 : scaled_x;
    int clamped_top_y = scaled_top_y < 0 ? 0 : scaled_top_y;
    if( clamped_x >= fb_width || clamped_top_y >= fb_height )
        return rect;

    int clamped_w = scaled_w;
    int clamped_h = scaled_h;
    if( clamped_x + clamped_w > fb_width )
        clamped_w = fb_width - clamped_x;
    if( clamped_top_y + clamped_h > fb_height )
        clamped_h = fb_height - clamped_top_y;
    if( clamped_w <= 0 || clamped_h <= 0 )
        return rect;

    rect.x      = clamped_x;
    rect.y      = fb_height - (clamped_top_y + clamped_h); // OpenGL bottom-left Y
    rect.width  = clamped_w;
    rect.height = clamped_h;
    return rect;
}

static uintptr_t
model_gpu_cache_key(const struct DashModel* model)
{
    if( !model )
        return 0;
#if UINTPTR_MAX == 0xffffffffu
    uintptr_t key = 2166136261u;
#else
    uintptr_t key = 1469598103934665603ull;
#endif
    const uintptr_t fnv_prime = (uintptr_t)16777619u;
    auto mix_word = [&](uintptr_t word) { key ^= word; key *= fnv_prime; };

    mix_word((uintptr_t)model->vertices_x);
    mix_word((uintptr_t)model->face_indices_a);
    mix_word((uintptr_t)model->face_indices_b);
    mix_word((uintptr_t)model->face_indices_c);
    mix_word((uintptr_t)model->face_count);

    const bool is_animated = model->original_vertices_x && model->original_vertices_y &&
                             model->original_vertices_z && model->vertex_count > 0;
    if( is_animated )
        for( int i = 0; i < model->vertex_count; ++i )
        {
            mix_word((uintptr_t)(uint32_t)model->vertices_x[i]);
            mix_word((uintptr_t)(uint32_t)model->vertices_y[i]);
            mix_word((uintptr_t)(uint32_t)model->vertices_z[i]);
        }
    return key;
}

// Same encoding as pix3dgl_load_texture (direction → sign, speed scale).
static float
metal_texture_animation_signed(int animation_direction, int animation_speed)
{
    if( animation_direction == 0 )
        return 0.0f;
    float speed = ((float)animation_speed) / (128.0f / 50.0f);
    if( animation_direction == 2 || animation_direction == 4 )
        return speed;
    return -speed;
}

// Append one face (3 vertices) in model draw order. Skips invisible / invalid faces.
// effective_tex_id is -1 for untextured, or a loaded texture id (caller drops missing atlas slots).
static void
append_model_face_vertices(
    const struct DashModel* model,
    int f,
    float world_x, float world_y, float world_z,
    float cos_yaw, float sin_yaw,
    int effective_tex_id,
    float texture_anim_speed,
    bool texture_opaque,
    std::vector<MetalVertex>& out)
{
    if( model->face_infos && model->face_infos[f] == 2 )
        return;
    if( model->lighting && model->lighting->face_colors_hsl_c &&
        model->lighting->face_colors_hsl_c[f] == -2 )
        return;

    const int ia = model->face_indices_a[f];
    const int ib = model->face_indices_b[f];
    const int ic = model->face_indices_c[f];
    if( ia < 0 || ia >= model->vertex_count || ib < 0 || ib >= model->vertex_count || ic < 0 ||
        ic >= model->vertex_count )
        return;

    if( !model->lighting || !model->lighting->face_colors_hsl_a ||
        !model->lighting->face_colors_hsl_b || !model->lighting->face_colors_hsl_c )
        return;

    int hsl_a = model->lighting->face_colors_hsl_a[f];
    int hsl_b = model->lighting->face_colors_hsl_b[f];
    int hsl_c = model->lighting->face_colors_hsl_c[f];
    int rgb_a, rgb_b, rgb_c;
    if( hsl_c == -1 )
        rgb_a = rgb_b = rgb_c = g_hsl16_to_rgb_table[hsl_a & 65535];
    else
    {
        rgb_a = g_hsl16_to_rgb_table[hsl_a & 65535];
        rgb_b = g_hsl16_to_rgb_table[hsl_b & 65535];
        rgb_c = g_hsl16_to_rgb_table[hsl_c & 65535];
    }

    float face_alpha = 1.0f;
    if( model->face_alphas )
    {
        int alpha_raw = model->face_alphas[f];
        if( alpha_raw >= 0 )
        {
            const int ab = alpha_raw & 0xFF;
            const bool textured =
                model->face_textures && model->face_textures[f] != -1 && effective_tex_id >= 0;
            if( textured )
                face_alpha = (float)ab / 255.0f;
            else
                face_alpha = (float)(0xFF - ab) / 255.0f;
        }
    }

    float u_corner[3] = { 0.0f, 0.0f, 0.0f };
    float v_corner[3] = { 0.0f, 0.0f, 0.0f };

    if( effective_tex_id >= 0 )
    {
        int texture_face_idx = f;
        int tp = 0, tm = 0, tn = 0;
        if( model->face_texture_coords && model->face_texture_coords[f] != -1 &&
            model->textured_p_coordinate && model->textured_m_coordinate &&
            model->textured_n_coordinate )
        {
            texture_face_idx = model->face_texture_coords[f];
            tp = model->textured_p_coordinate[texture_face_idx];
            tm = model->textured_m_coordinate[texture_face_idx];
            tn = model->textured_n_coordinate[texture_face_idx];
        }
        else
        {
            tp = model->face_indices_a[texture_face_idx];
            tm = model->face_indices_b[texture_face_idx];
            tn = model->face_indices_c[texture_face_idx];
        }

        struct UVFaceCoords uv;
        uv_pnm_compute(
            &uv,
            (float)model->vertices_x[tp],
            (float)model->vertices_y[tp],
            (float)model->vertices_z[tp],
            (float)model->vertices_x[tm],
            (float)model->vertices_y[tm],
            (float)model->vertices_z[tm],
            (float)model->vertices_x[tn],
            (float)model->vertices_y[tn],
            (float)model->vertices_z[tn],
            (float)model->vertices_x[ia],
            (float)model->vertices_y[ia],
            (float)model->vertices_z[ia],
            (float)model->vertices_x[ib],
            (float)model->vertices_y[ib],
            (float)model->vertices_z[ib],
            (float)model->vertices_x[ic],
            (float)model->vertices_y[ic],
            (float)model->vertices_z[ic]);
        u_corner[0] = uv.u1;
        u_corner[1] = uv.u2;
        u_corner[2] = uv.u3;
        v_corner[0] = uv.v1;
        v_corner[1] = uv.v2;
        v_corner[2] = uv.v3;
    }

    const float texBlend = effective_tex_id >= 0 ? 1.0f : 0.0f;
    const float opaque_f = texture_opaque ? 1.0f : 0.0f;

    const int verts[3] = { ia, ib, ic };
    const int rgbs[3] = { rgb_a, rgb_b, rgb_c };
    for( int vi = 0; vi < 3; ++vi )
    {
        const int vi_idx = verts[vi];
        float lx = (float)model->vertices_x[vi_idx];
        float ly = (float)model->vertices_y[vi_idx];
        float lz = (float)model->vertices_z[vi_idx];

        MetalVertex mv;
        mv.position[0] = cos_yaw * lx + sin_yaw * lz + world_x;
        mv.position[1] = ly + world_y;
        mv.position[2] = -sin_yaw * lx + cos_yaw * lz + world_z;
        mv.position[3] = 1.0f;

        int rgb = rgbs[vi];
        mv.color[0] = ((rgb >> 16) & 0xFF) / 255.0f;
        mv.color[1] = ((rgb >> 8) & 0xFF) / 255.0f;
        mv.color[2] = (rgb & 0xFF) / 255.0f;
        mv.color[3] = face_alpha;

        mv.texcoord[0] = u_corner[vi];
        mv.texcoord[1] = v_corner[vi];
        mv.texBlend = texBlend;
        mv.textureOpaque = opaque_f;
        mv.textureAnimSpeed = texture_anim_speed;
        mv._pad_vertex = 0.0f;
        out.push_back(mv);
    }
}

// Pre-cache placeholder (we still track load keys, but actual vertex buffers
// are built per-draw with the correct world offset so this is a no-op).
static void
preload_model_key(
    struct Platform2_OSX_SDL2_Renderer_Metal* renderer,
    const struct DashModel* model)
{
    if( !model )
        return;
    uintptr_t key = model_gpu_cache_key(model);
    renderer->loaded_model_keys.insert(key);
    if( renderer->model_index_by_key.find(key) == renderer->model_index_by_key.end() )
        renderer->model_index_by_key[key] = renderer->next_model_index++;
}

// ---------------------------------------------------------------------------
// ImGui overlay
// ---------------------------------------------------------------------------
static void
render_imgui_overlay(
    struct Platform2_OSX_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    id<MTLCommandBuffer> commandBuffer,
    id<MTLRenderCommandEncoder> encoder,
    MTLRenderPassDescriptor* renderPassDesc)
{
    ImGui_ImplMetal_NewFrame(renderPassDesc);
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 220), ImGuiCond_FirstUseEver);
    ImGui::Begin("Metal Debug");
    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate,
        ImGui::GetIO().Framerate);
    ImGui::Text(
        "Camera: %d %d %d", game->camera_world_x, game->camera_world_y, game->camera_world_z);
    ImGui::Text("Mouse: %d %d", game->mouse_x, game->mouse_y);
    if( game->view_port )
    {
        ImGui::Separator();
        int w = game->view_port->width;
        int h = game->view_port->height;
        bool changed = ImGui::InputInt("World viewport W", &w);
        changed |= ImGui::InputInt("World viewport H", &h);
        if( changed )
        {
            if( w > 4096 ) w = 4096;
            if( h > 4096 ) h = 4096;
            LibToriRS_GameSetWorldViewportSize(game, w, h);
        }
    }
    ImGui::Text("Loaded model keys: %zu",   renderer->loaded_model_keys.size());
    ImGui::Text("Loaded scene keys: %zu",   renderer->loaded_scene_element_keys.size());
    ImGui::Text("Loaded textures: %zu",     renderer->loaded_texture_ids.size());
    ImGui::Separator();
    ImGui::Text("Frame model draws: %u",    renderer->debug_model_draws);
    ImGui::Text("Frame triangles: %u",      renderer->debug_triangles);
    ImGui::End();

    ImGui::Render();
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, encoder);
}

// ---------------------------------------------------------------------------
// Sync drawable size from SDL Metal view
// ---------------------------------------------------------------------------
static void
sync_drawable_size(struct Platform2_OSX_SDL2_Renderer_Metal* renderer)
{
    if( !renderer || !renderer->metal_view )
        return;

    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(renderer->metal_view);
    if( !layer )
        return;

    CGSize sz = layer.drawableSize;
    if( sz.width > 0  ) renderer->width  = (int)sz.width;
    if( sz.height > 0 ) renderer->height = (int)sz.height;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

struct Platform2_OSX_SDL2_Renderer_Metal*
PlatformImpl2_OSX_SDL2_Renderer_Metal_New(int width, int height)
{
    auto* renderer = new Platform2_OSX_SDL2_Renderer_Metal();
    renderer->mtl_device         = nil;
    renderer->mtl_command_queue  = nil;
    renderer->mtl_pipeline_state      = nil;
    renderer->mtl_ui_sprite_pipeline  = nil;
    renderer->mtl_depth_stencil       = nil;
    renderer->mtl_uniform_buffer = nil;
    renderer->mtl_sampler_state  = nil;
    renderer->mtl_dummy_texture  = nil;
    renderer->metal_view         = nullptr;
    renderer->platform           = nullptr;
    renderer->width              = width;
    renderer->height             = height;
    renderer->metal_ready        = false;
    renderer->debug_model_draws  = 0;
    renderer->debug_triangles    = 0;
    renderer->next_model_index   = 1;
    return renderer;
}

void
PlatformImpl2_OSX_SDL2_Renderer_Metal_Free(
    struct Platform2_OSX_SDL2_Renderer_Metal* renderer)
{
    if( !renderer )
        return;

    // Release cached textures
    for( auto& kv : renderer->texture_by_id )
        if( kv.second )
            CFRelease(kv.second);

    ImGui_ImplMetal_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    // ARC manages the Metal objects; clear pointers so the bridged refs release
    renderer->texture_anim_speed_by_id.clear();
    renderer->texture_opaque_by_id.clear();

    if( renderer->mtl_dummy_texture )
    {
        CFRelease(renderer->mtl_dummy_texture);
        renderer->mtl_dummy_texture = nullptr;
    }
    if( renderer->mtl_sampler_state )
    {
        CFRelease(renderer->mtl_sampler_state);
        renderer->mtl_sampler_state = nullptr;
    }
    if( renderer->mtl_uniform_buffer )
    {
        CFRelease(renderer->mtl_uniform_buffer);
        renderer->mtl_uniform_buffer = nullptr;
    }
    if( renderer->mtl_depth_stencil )
    {
        CFRelease(renderer->mtl_depth_stencil);
        renderer->mtl_depth_stencil = nullptr;
    }
    if( renderer->mtl_ui_sprite_pipeline )
    {
        CFRelease(renderer->mtl_ui_sprite_pipeline);
        renderer->mtl_ui_sprite_pipeline = nullptr;
    }
    if( renderer->mtl_pipeline_state )
    {
        CFRelease(renderer->mtl_pipeline_state);
        renderer->mtl_pipeline_state = nullptr;
    }
    if( renderer->mtl_command_queue )
    {
        CFRelease(renderer->mtl_command_queue);
        renderer->mtl_command_queue = nullptr;
    }
    if( renderer->mtl_device )
    {
        CFRelease(renderer->mtl_device);
        renderer->mtl_device = nullptr;
    }

    if( renderer->metal_view )
    {
        SDL_Metal_DestroyView(renderer->metal_view);
        renderer->metal_view = nullptr;
    }

    delete renderer;
}

bool
PlatformImpl2_OSX_SDL2_Renderer_Metal_Init(
    struct Platform2_OSX_SDL2_Renderer_Metal* renderer,
    struct Platform2_OSX_SDL2* platform)
{
    if( !renderer || !platform || !platform->window )
        return false;

    renderer->platform   = platform;
    renderer->metal_view = SDL_Metal_CreateView(platform->window);
    if( !renderer->metal_view )
    {
        printf("Metal init failed: SDL_Metal_CreateView returned null: %s\n", SDL_GetError());
        return false;
    }

    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(renderer->metal_view);
    if( !layer )
    {
        printf("Metal init failed: could not obtain CAMetalLayer\n");
        return false;
    }

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if( !device )
    {
        printf("Metal init failed: MTLCreateSystemDefaultDevice returned nil\n");
        return false;
    }
    layer.device = device;
    renderer->mtl_device = (__bridge_retained void*)device;

    id<MTLCommandQueue> queue = [device newCommandQueue];
    renderer->mtl_command_queue = (__bridge_retained void*)queue;

    // -----------------------------------------------------------------------
    // Pipeline state — load Shaders.metal compiled library
    // -----------------------------------------------------------------------
    NSError* error = nil;

    // Resolve Shaders.metallib: same directory as this binary (CMake places it there),
    // then cwd, then bundle resources / source-tree fallbacks.
    NSMutableArray<NSString*>* candidates = [NSMutableArray array];
    NSString* exePath = [[NSBundle mainBundle] executablePath];
    if( exePath.length > 0 )
    {
        NSString* exeDir = [exePath stringByDeletingLastPathComponent];
        [candidates addObject:[exeDir stringByAppendingPathComponent:@"Shaders.metallib"]];
    }
    NSString* bundleShader = [[NSBundle mainBundle] pathForResource:@"Shaders" ofType:@"metallib"];
    if( bundleShader.length > 0 )
        [candidates addObject:bundleShader];
    [candidates addObject:@"Shaders.metallib"];
    [candidates addObject:@"../Shaders.metallib"];
    [candidates addObject:@"../src/Shaders.metallib"];
    NSArray<NSString*>* candidatePaths = candidates;

    id<MTLLibrary> shaderLibrary = nil;
    for( NSString* path in candidatePaths )
    {
        if( path.length == 0 ) continue;
        NSData* data = [NSData dataWithContentsOfFile:path];
        if( !data ) continue;
        dispatch_data_t dd = dispatch_data_create(
            data.bytes, data.length,
            dispatch_get_main_queue(),
            DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        shaderLibrary = [device newLibraryWithData:dd error:&error];
        if( shaderLibrary ) break;
    }

    if( !shaderLibrary )
    {
        printf("Metal init failed: could not load Shaders.metallib: %s\n",
               error ? error.localizedDescription.UTF8String : "unknown");
        return false;
    }

    id<MTLFunction> vertFn = [shaderLibrary newFunctionWithName:@"vertexShader"];
    id<MTLFunction> fragFn = [shaderLibrary newFunctionWithName:@"fragmentShader"];
    if( !vertFn || !fragFn )
    {
        printf("Metal init failed: shader functions not found in library\n");
        return false;
    }

    // Vertex descriptor matching MetalVertex layout (see Shaders.metal struct Vertex)
    MTLVertexDescriptor* vtxDesc = [[MTLVertexDescriptor alloc] init];
    vtxDesc.attributes[0].format      = MTLVertexFormatFloat4;
    vtxDesc.attributes[0].offset      = offsetof(MetalVertex, position);
    vtxDesc.attributes[0].bufferIndex = 0;
    vtxDesc.attributes[1].format      = MTLVertexFormatFloat4;
    vtxDesc.attributes[1].offset      = offsetof(MetalVertex, color);
    vtxDesc.attributes[1].bufferIndex = 0;
    vtxDesc.attributes[2].format      = MTLVertexFormatFloat2;
    vtxDesc.attributes[2].offset      = offsetof(MetalVertex, texcoord);
    vtxDesc.attributes[2].bufferIndex = 0;
    vtxDesc.attributes[3].format      = MTLVertexFormatFloat;
    vtxDesc.attributes[3].offset      = offsetof(MetalVertex, texBlend);
    vtxDesc.attributes[3].bufferIndex = 0;
    vtxDesc.attributes[4].format      = MTLVertexFormatFloat;
    vtxDesc.attributes[4].offset      = offsetof(MetalVertex, textureOpaque);
    vtxDesc.attributes[4].bufferIndex = 0;
    vtxDesc.attributes[5].format      = MTLVertexFormatFloat;
    vtxDesc.attributes[5].offset      = offsetof(MetalVertex, textureAnimSpeed);
    vtxDesc.attributes[5].bufferIndex = 0;
    vtxDesc.layouts[0].stride         = sizeof(MetalVertex);
    vtxDesc.layouts[0].stepFunction   = MTLVertexStepFunctionPerVertex;

    MTLRenderPipelineDescriptor* pipeDesc = [[MTLRenderPipelineDescriptor alloc] init];
    pipeDesc.vertexFunction   = vertFn;
    pipeDesc.fragmentFunction = fragFn;
    pipeDesc.vertexDescriptor = vtxDesc;
    pipeDesc.colorAttachments[0].pixelFormat = layer.pixelFormat;

    // Enable alpha blending for transparent faces
    pipeDesc.colorAttachments[0].blendingEnabled             = YES;
    pipeDesc.colorAttachments[0].rgbBlendOperation           = MTLBlendOperationAdd;
    pipeDesc.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
    pipeDesc.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
    pipeDesc.colorAttachments[0].alphaBlendOperation         = MTLBlendOperationAdd;
    pipeDesc.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorOne;
    pipeDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipeDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

    id<MTLRenderPipelineState> pipeState = [device newRenderPipelineStateWithDescriptor:pipeDesc error:&error];
    if( !pipeState )
    {
        printf("Metal init failed: could not create pipeline state: %s\n",
               error ? error.localizedDescription.UTF8String : "unknown");
        return false;
    }
    renderer->mtl_pipeline_state = (__bridge_retained void*)pipeState;

    id<MTLFunction> uiVertFn = [shaderLibrary newFunctionWithName:@"uiSpriteVert"];
    id<MTLFunction> uiFragFn = [shaderLibrary newFunctionWithName:@"uiSpriteFrag"];
    if( uiVertFn && uiFragFn )
    {
        MTLVertexDescriptor* uiVtx = [[MTLVertexDescriptor alloc] init];
        uiVtx.attributes[0].format      = MTLVertexFormatFloat2;
        uiVtx.attributes[0].offset      = 0;
        uiVtx.attributes[0].bufferIndex = 0;
        uiVtx.attributes[1].format      = MTLVertexFormatFloat2;
        uiVtx.attributes[1].offset      = 8;
        uiVtx.attributes[1].bufferIndex = 0;
        uiVtx.layouts[0].stride         = 16;
        uiVtx.layouts[0].stepFunction   = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* uiPipeDesc = [[MTLRenderPipelineDescriptor alloc] init];
        uiPipeDesc.vertexFunction                 = uiVertFn;
        uiPipeDesc.fragmentFunction               = uiFragFn;
        uiPipeDesc.vertexDescriptor               = uiVtx;
        uiPipeDesc.colorAttachments[0].pixelFormat = layer.pixelFormat;
        uiPipeDesc.colorAttachments[0].blendingEnabled             = YES;
        uiPipeDesc.colorAttachments[0].rgbBlendOperation           = MTLBlendOperationAdd;
        uiPipeDesc.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
        uiPipeDesc.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
        uiPipeDesc.colorAttachments[0].alphaBlendOperation         = MTLBlendOperationAdd;
        uiPipeDesc.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorOne;
        uiPipeDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        uiPipeDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        NSError* uiErr  = nil;
        id<MTLRenderPipelineState> uiPipe =
            [device newRenderPipelineStateWithDescriptor:uiPipeDesc error:&uiErr];
        if( uiPipe )
            renderer->mtl_ui_sprite_pipeline = (__bridge_retained void*)uiPipe;
        else
            printf(
                "Metal: UI sprite pipeline creation failed: %s\n",
                uiErr ? uiErr.localizedDescription.UTF8String : "unknown");
    }

    // Match pix3dgl_new(false, true): z-buffer off → GL_ALWAYS / no depth write
    MTLDepthStencilDescriptor* dsDesc = [[MTLDepthStencilDescriptor alloc] init];
    dsDesc.depthCompareFunction = MTLCompareFunctionAlways;
    dsDesc.depthWriteEnabled    = NO;
    id<MTLDepthStencilState> dsState = [device newDepthStencilStateWithDescriptor:dsDesc];
    renderer->mtl_depth_stencil = (__bridge_retained void*)dsState;

    // Uniform buffer
    id<MTLBuffer> unifBuf = [device newBufferWithLength:sizeof(MetalUniforms)
                                                options:MTLResourceStorageModeShared];
    renderer->mtl_uniform_buffer = (__bridge_retained void*)unifBuf;

    MTLSamplerDescriptor* sampDesc = [[MTLSamplerDescriptor alloc] init];
    sampDesc.minFilter              = MTLSamplerMinMagFilterLinear;
    sampDesc.magFilter              = MTLSamplerMinMagFilterLinear;
    sampDesc.sAddressMode           = MTLSamplerAddressModeClampToEdge;
    sampDesc.tAddressMode           = MTLSamplerAddressModeRepeat;
    id<MTLSamplerState> sampState   = [device newSamplerStateWithDescriptor:sampDesc];
    renderer->mtl_sampler_state     = (__bridge_retained void*)sampState;

    MTLTextureDescriptor* dumDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:1
                                                          height:1
                                                       mipmapped:NO];
    dumDesc.usage       = MTLTextureUsageShaderRead;
    dumDesc.storageMode = MTLStorageModeShared;
    id<MTLTexture> dumTex = [device newTextureWithDescriptor:dumDesc];
    const uint8_t whiteRgba[4] = { 255, 255, 255, 255 };
    [dumTex replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
              mipmapLevel:0
                withBytes:whiteRgba
              bytesPerRow:4];
    renderer->mtl_dummy_texture = (__bridge_retained void*)dumTex;

    // Sync initial drawable size
    sync_drawable_size(renderer);

    // -----------------------------------------------------------------------
    // ImGui
    // -----------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForMetal(platform->window);
    ImGui_ImplMetal_Init(device);

    renderer->metal_ready = true;
    return true;
}

void
PlatformImpl2_OSX_SDL2_Renderer_Metal_Render(
    struct Platform2_OSX_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !renderer->metal_ready || !game || !render_command_buffer ||
        !renderer->platform || !renderer->platform->window )
        return;

    id<MTLDevice>             device    = (__bridge id<MTLDevice>)renderer->mtl_device;
    id<MTLCommandQueue>       queue     = (__bridge id<MTLCommandQueue>)renderer->mtl_command_queue;
    id<MTLRenderPipelineState> pipeState = (__bridge id<MTLRenderPipelineState>)renderer->mtl_pipeline_state;
    id<MTLDepthStencilState>  dsState   = (__bridge id<MTLDepthStencilState>)renderer->mtl_depth_stencil;
    id<MTLBuffer>             unifBuf   = (__bridge id<MTLBuffer>)renderer->mtl_uniform_buffer;

    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(renderer->metal_view);
    if( !layer )
        return;

    sync_drawable_size(renderer);

    // Resize the depth texture if the drawable size changed
    layer.drawableSize = CGSizeMake(renderer->width, renderer->height);

    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if( !drawable )
        return;

    renderer->debug_model_draws = 0;
    renderer->debug_triangles   = 0;

    // -----------------------------------------------------------------------
    // Build render pass descriptor with depth attachment
    // -----------------------------------------------------------------------
    MTLTextureDescriptor* depthTexDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                           width:(NSUInteger)renderer->width
                                                          height:(NSUInteger)renderer->height
                                                       mipmapped:NO];
    depthTexDesc.storageMode = MTLStorageModePrivate;
    depthTexDesc.usage       = MTLTextureUsageRenderTarget;
    id<MTLTexture> depthTex  = [device newTextureWithDescriptor:depthTexDesc];

    MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    rpDesc.colorAttachments[0].texture     = drawable.texture;
    rpDesc.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpDesc.colorAttachments[0].clearColor  = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    rpDesc.depthAttachment.texture         = depthTex;
    rpDesc.depthAttachment.loadAction      = MTLLoadActionClear;
    rpDesc.depthAttachment.storeAction     = MTLStoreActionDontCare;
    rpDesc.depthAttachment.clearDepth      = 1.0;

    // -----------------------------------------------------------------------
    // Compute viewport rects
    // -----------------------------------------------------------------------
    int win_width  = renderer->platform->game_screen_width;
    int win_height = renderer->platform->game_screen_height;
    if( win_width <= 0 || win_height <= 0 )
        SDL_GetWindowSize(renderer->platform->window, &win_width, &win_height);

    const LogicalViewportRect logical_vp =
        compute_logical_viewport_rect(win_width, win_height, game);
    const MTLViewportRect gl_vp = compute_gl_world_viewport_rect(
        renderer->width, renderer->height, win_width, win_height, logical_vp);

    const float projection_width  = (float)logical_vp.width;
    const float projection_height = (float)logical_vp.height;

    // Same as pix3dgl_begin_3dframe(..., 0,0,0, camera_pitch, camera_yaw, ...):
    // game angles are OSRS units (2048 = 2*pi); camera position stays at origin.
    MetalUniforms uniforms;
    const float pitch_rad =
        ((float)game->camera_pitch * 2.0f * (float)M_PI) / 2048.0f;
    const float yaw_rad =
        ((float)game->camera_yaw * 2.0f * (float)M_PI) / 2048.0f;
    metal_compute_view_matrix(uniforms.modelViewMatrix, 0.0f, 0.0f, 0.0f, pitch_rad, yaw_rad);
    metal_compute_projection_matrix(
        uniforms.projectionMatrix,
        (90.0f * (float)M_PI) / 180.0f,
        projection_width,
        projection_height);
    metal_remap_projection_opengl_to_metal_z(uniforms.projectionMatrix);
    uniforms.uClock = (float)(SDL_GetTicks64() / 1000.0);
    uniforms._pad_uniform[0] = 0.0f;
    uniforms._pad_uniform[1] = 0.0f;
    uniforms._pad_uniform[2] = 0.0f;
    memcpy(unifBuf.contents, &uniforms, sizeof(uniforms));

    // -----------------------------------------------------------------------
    // Command buffer + render encoder
    // -----------------------------------------------------------------------
    id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder =
        [cmdBuf renderCommandEncoderWithDescriptor:rpDesc];

    [encoder setRenderPipelineState:pipeState];
    [encoder setDepthStencilState:dsState];
    // Metal viewport Y vs GL + column-major clip can invert winding; match painter order like z-off GL.
    [encoder setCullMode:MTLCullModeNone];

    // Metal viewport: top-left origin. gl_vp.y is OpenGL bottom-left Y.
    const double metal_origin_y =
        (double)renderer->height - (double)gl_vp.y - (double)gl_vp.height;
    MTLViewport metalVp = {
        .originX = (double)gl_vp.x,
        .originY = metal_origin_y,
        .width   = (double)gl_vp.width,
        .height  = (double)gl_vp.height,
        .znear   = 0.0,
        .zfar    = 1.0
    };
    [encoder setViewport:metalVp];

    // -----------------------------------------------------------------------
    // Drain the render command buffer (three-pass, same as OpenGL3 renderer)
    // -----------------------------------------------------------------------
    LibToriRS_FrameBegin(game, render_command_buffer);

    static std::vector<ToriRSRenderCommand> commands;
    commands.clear();
    {
        struct ToriRSRenderCommand cmd = { 0 };
        while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, false) )
            commands.push_back(cmd);
    }

    const int total_commands = (int)commands.size();

    // Pass 1: texture loads
    for( int i = 0; i < total_commands; ++i )
    {
        const ToriRSRenderCommand* cmd = &commands[i];
        if( cmd->kind != TORIRS_GFX_TEXTURE_LOAD )
            continue;

        const int tex_id = cmd->_texture_load.texture_id;
        renderer->loaded_texture_ids.insert(tex_id);
        struct DashTexture* tex = cmd->_texture_load.texture_nullable;
        if( !tex || !tex->texels )
            continue;

        // Create/replace Metal texture
        if( renderer->texture_by_id.count(tex_id) && renderer->texture_by_id[tex_id] )
            CFRelease(renderer->texture_by_id[tex_id]);

        renderer->texture_anim_speed_by_id[tex_id] =
            metal_texture_animation_signed(tex->animation_direction, tex->animation_speed);
        renderer->texture_opaque_by_id[tex_id] = tex->opaque;

        const int w = tex->width;
        const int h = tex->height;
        std::vector<uint8_t> rgba((size_t)w * (size_t)h * 4u);
        for( int p = 0; p < w * h; ++p )
        {
            int pix = tex->texels[p];
            rgba[(size_t)p * 4u + 0] = (uint8_t)((pix >> 16) & 0xFF);
            rgba[(size_t)p * 4u + 1] = (uint8_t)((pix >> 8) & 0xFF);
            rgba[(size_t)p * 4u + 2] = (uint8_t)(pix & 0xFF);
            rgba[(size_t)p * 4u + 3] = (uint8_t)((pix >> 24) & 0xFF);
        }

        MTLTextureDescriptor* texDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:(NSUInteger)w
                                                              height:(NSUInteger)h
                                                           mipmapped:NO];
        texDesc.usage       = MTLTextureUsageShaderRead;
        texDesc.storageMode = MTLStorageModeShared;
        id<MTLTexture> mtlTex = [device newTextureWithDescriptor:texDesc];
        [mtlTex replaceRegion:MTLRegionMake2D(0, 0, w, h)
                  mipmapLevel:0
                    withBytes:rgba.data()
                  bytesPerRow:(NSUInteger)w * 4];
        renderer->texture_by_id[tex_id] = (__bridge_retained void*)mtlTex;
    }

    // Pass 2: model loads — record cache keys (vertex buffers are built per-draw)
    for( int i = 0; i < total_commands; ++i )
    {
        const ToriRSRenderCommand* cmd = &commands[i];
        if( cmd->kind != TORIRS_GFX_MODEL_LOAD )
            continue;

        struct DashModel* model = cmd->_model_load.model;
        if( !model || !model->lighting || !model->vertices_x || !model->vertices_y ||
            !model->vertices_z || !model->face_indices_a || !model->face_indices_b ||
            !model->face_indices_c || model->face_count <= 0 )
            continue;

        preload_model_key(renderer, model);
    }

    // Pass 3: model draws — batch by texture id so each draw has one bound texture (like atlas).
    static std::vector<MetalVertex> batch_verts;
    batch_verts.clear();
    int batch_tex_id = -0x7fffffff; // sentinel: force first flush path

    id<MTLTexture> dummyTex = (__bridge id<MTLTexture>)renderer->mtl_dummy_texture;
    id<MTLSamplerState> samp = (__bridge id<MTLSamplerState>)renderer->mtl_sampler_state;

    auto flush_batch = [&]() {
        if( batch_verts.empty() )
            return;
        id<MTLTexture> bindTex = dummyTex;
        if( batch_tex_id >= 0 )
        {
            auto it = renderer->texture_by_id.find(batch_tex_id);
            if( it == renderer->texture_by_id.end() || !it->second )
            {
                batch_verts.clear();
                return;
            }
            bindTex = (__bridge id<MTLTexture>)it->second;
        }
        [encoder setFragmentTexture:bindTex atIndex:0];
        [encoder setFragmentSamplerState:samp atIndex:0];
        id<MTLBuffer> drawBuf =
            [device newBufferWithBytes:batch_verts.data()
                                 length:(NSUInteger)(batch_verts.size() * sizeof(MetalVertex))
                                options:MTLResourceStorageModeShared];
        [encoder setVertexBuffer:drawBuf offset:0 atIndex:0];
        [encoder setVertexBuffer:unifBuf offset:0 atIndex:1];
        [encoder setFragmentBuffer:unifBuf offset:0 atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:(NSUInteger)batch_verts.size()];
        renderer->debug_triangles += (unsigned int)(batch_verts.size() / 3);
        batch_verts.clear();
    };

    for( int i = 0; i < total_commands; ++i )
    {
        const ToriRSRenderCommand* cmd = &commands[i];
        if( cmd->kind != TORIRS_GFX_MODEL_DRAW )
            continue;

        struct DashModel* model = cmd->_model_draw.model;
        if( !model || !model->lighting || !model->vertices_x || !model->vertices_y ||
            !model->vertices_z || !model->face_indices_a || !model->face_indices_b ||
            !model->face_indices_c || model->face_count <= 0 )
            continue;

        preload_model_key(renderer, model);

        struct DashPosition draw_position = cmd->_model_draw.position;
        const int cull = dash3d_project_model(
            game->sys_dash, model, &draw_position, game->view_port, game->camera);
        if( cull != DASHCULL_VISIBLE )
            continue;

        int face_order_count = dash3d_prepare_projected_face_order(
            game->sys_dash, model, &draw_position, game->view_port, game->camera);
        const int* face_order = dash3d_projected_face_order(game->sys_dash, &face_order_count);

        const float yaw_rad = (draw_position.yaw * 2.0f * (float)M_PI) / 2048.0f;
        const float cos_yaw = cosf(yaw_rad);
        const float sin_yaw = sinf(yaw_rad);

        renderer->debug_model_draws++;
        for( int fi = 0; fi < face_order_count; ++fi )
        {
            const int f = face_order ? face_order[fi] : fi;
            if( f < 0 || f >= model->face_count )
                continue;

            int raw_tex = model->face_textures ? model->face_textures[f] : -1;
            int eff_tex = raw_tex;
            if( eff_tex >= 0 && renderer->texture_by_id.find(eff_tex) == renderer->texture_by_id.end() )
                eff_tex = -1;

            float anim_spd = 0.0f;
            bool tex_opaque = true;
            if( eff_tex >= 0 )
            {
                auto as_it = renderer->texture_anim_speed_by_id.find(eff_tex);
                if( as_it != renderer->texture_anim_speed_by_id.end() )
                    anim_spd = as_it->second;
                auto op_it = renderer->texture_opaque_by_id.find(eff_tex);
                if( op_it != renderer->texture_opaque_by_id.end() )
                    tex_opaque = op_it->second;
            }

            const int batch_key = eff_tex;
            if( !batch_verts.empty() && batch_key != batch_tex_id )
                flush_batch();
            batch_tex_id = batch_key;

            const size_t before = batch_verts.size();
            append_model_face_vertices(
                model,
                f,
                (float)draw_position.x,
                (float)draw_position.y,
                (float)draw_position.z,
                cos_yaw,
                sin_yaw,
                eff_tex,
                anim_spd,
                tex_opaque,
                batch_verts);
            if( batch_verts.size() == before )
                continue;
        }
    }
    flush_batch();

    id<MTLRenderPipelineState> uiPipeState =
        renderer->mtl_ui_sprite_pipeline
            ? (__bridge id<MTLRenderPipelineState>)renderer->mtl_ui_sprite_pipeline
            : nil;
    id<MTLSamplerState> uiSampler = (__bridge id<MTLSamplerState>)renderer->mtl_sampler_state;
    if( uiPipeState )
    {
        MTLViewport spriteVp = {
            .originX = 0.0,
            .originY = 0.0,
            .width   = (double)renderer->width,
            .height  = (double)renderer->height,
            .znear   = 0.0,
            .zfar    = 1.0
        };
        [encoder setViewport:spriteVp];
        [encoder setRenderPipelineState:uiPipeState];
        [encoder setDepthStencilState:dsState];
        [encoder setCullMode:MTLCullModeNone];

        for( int i = 0; i < total_commands; ++i )
        {
            const ToriRSRenderCommand* cmd = &commands[i];
            if( cmd->kind != TORIRS_GFX_SPRITE_DRAW )
                continue;
            struct DashSprite* sp = cmd->_sprite_draw.sprite;
            if( !sp || !sp->pixels_argb || sp->width <= 0 || sp->height <= 0 )
                continue;

            const int tw = sp->width;
            const int th = sp->height;
            std::vector<uint8_t> rgba((size_t)tw * (size_t)th * 4u);
            for( int p = 0; p < tw * th; ++p )
            {
                // Sprite pixels are 0x00RRGGBB; pixel == 0 is the transparent color key.
                // Synthesize alpha so the uiSpriteFrag discard (c.a < 0.01) works correctly.
                uint32_t pix = sp->pixels_argb[p];
                rgba[(size_t)p * 4u + 0u] = (uint8_t)((pix >> 16) & 0xFFu);
                rgba[(size_t)p * 4u + 1u] = (uint8_t)((pix >> 8) & 0xFFu);
                rgba[(size_t)p * 4u + 2u] = (uint8_t)(pix & 0xFFu);
                rgba[(size_t)p * 4u + 3u] = (pix != 0u) ? 0xFFu : 0x00u;
            }

            MTLTextureDescriptor* td =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                   width:(NSUInteger)tw
                                                                  height:(NSUInteger)th
                                                               mipmapped:NO];
            td.usage       = MTLTextureUsageShaderRead;
            td.storageMode = MTLStorageModeShared;
            id<MTLTexture> spriteTex = [device newTextureWithDescriptor:td];
            [spriteTex replaceRegion:MTLRegionMake2D(0, 0, tw, th)
                         mipmapLevel:0
                           withBytes:rgba.data()
                         bytesPerRow:(NSUInteger)tw * 4u];

            const int dst_x = cmd->_sprite_draw.x + sp->crop_x;
            const int dst_y = cmd->_sprite_draw.y + sp->crop_y;
            const float w   = (float)tw;
            const float h   = (float)th;
            const float x0  = (float)dst_x;
            const float y0  = (float)dst_y;
            const float x1  = x0 + w;
            const float y1  = y0 + h;
            const float cx  = 0.5f * (x0 + x1);
            const float cy  = 0.5f * (y0 + y1);
            const float angle =
                (float)(cmd->_sprite_draw.rotation_r2pi2048 * (2.0 * M_PI) / 2048.0);
            const float ca = cosf(angle);
            const float sa = sinf(angle);
            const float hw = 0.5f * w;
            const float hh = 0.5f * h;

            float px[4];
            float py[4];
            auto rot_local = [&](float lx, float ly, int k) {
                px[k] = cx + ca * lx - sa * ly;
                py[k] = cy + sa * lx + ca * ly;
            };
            rot_local(-hw, -hh, 0);
            rot_local(hw, -hh, 1);
            rot_local(hw, hh, 2);
            rot_local(-hw, hh, 3);

            const float fbw = (float)(win_width  > 0 ? win_width  : renderer->width);
            const float fbh = (float)(win_height > 0 ? win_height : renderer->height);
            auto to_clip = [&](float xp, float yp, float* ocx, float* ocy) {
                *ocx = 2.0f * xp / fbw - 1.0f;
                *ocy = 1.0f - 2.0f * yp / fbh;
            };

            float c0x, c0y, c1x, c1y, c2x, c2y, c3x, c3y;
            to_clip(px[0], py[0], &c0x, &c0y);
            to_clip(px[1], py[1], &c1x, &c1y);
            to_clip(px[2], py[2], &c2x, &c2y);
            to_clip(px[3], py[3], &c3x, &c3y);

            float verts[6 * 4] = {
                c0x, c0y, 0.0f, 0.0f, c1x, c1y, 1.0f, 0.0f, c2x, c2y, 1.0f, 1.0f,
                c0x, c0y, 0.0f, 0.0f, c2x, c2y, 1.0f, 1.0f, c3x, c3y, 0.0f, 1.0f,
            };

            id<MTLBuffer> vb =
                [device newBufferWithBytes:verts
                                    length:sizeof(verts)
                                   options:MTLResourceStorageModeShared];
            [encoder setVertexBuffer:vb offset:0 atIndex:0];
            [encoder setFragmentTexture:spriteTex atIndex:0];
            [encoder setFragmentSamplerState:uiSampler atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:6];
        }
    }

    // -----------------------------------------------------------------------
    // Finish scene pass, render ImGui overlay in the same render encoder
    // -----------------------------------------------------------------------
    // Reset viewport to full drawable for ImGui
    MTLViewport fullVp = {
        .originX = 0.0, .originY = 0.0,
        .width   = (double)renderer->width,
        .height  = (double)renderer->height,
        .znear   = 0.0, .zfar = 1.0
    };
    [encoder setViewport:fullVp];

    render_imgui_overlay(renderer, game, cmdBuf, encoder, rpDesc);

    LibToriRS_FrameEnd(game);

    [encoder endEncoding];
    [cmdBuf presentDrawable:drawable];
    [cmdBuf commit];
}
