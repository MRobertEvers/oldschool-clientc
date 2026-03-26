

#include "platform_impl2_emscripten_sdl2_renderer_soft3d.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

extern "C" {
#include "graphics/dash.h"
#include "osrs/game.h"
#include "platforms/common/platform_memory.h"
#include "tori_rs.h"
}

#include "tori_rs_render.h"

#include <assert.h>
#include <vector>

extern int g_trap_command;
extern int g_trap_x;
extern int g_trap_z;

static bool g_show_collision_map = false;

static void
blit_rotated_buffer(
    int* src_buffer,
    int src_width,
    int src_height,
    int src_anchor_x,
    int src_anchor_y,
    int* dst_buffer,
    int dst_stride,
    int dst_x,
    int dst_y,
    int dst_width,
    int dst_height,
    int dst_anchor_x,
    int dst_anchor_y,
    int angle_r2pi2048)
{
    assert(dst_width + dst_x <= dst_stride);
    int sin = dash_sin(angle_r2pi2048);
    int cos = dash_cos(angle_r2pi2048);

    int min_x = dst_x;
    int min_y = dst_y;
    int max_x = dst_x + dst_width;
    int max_y = dst_y + dst_height;

    if( max_x > dst_stride )
        max_x = dst_stride;

    for( int dst_y_abs = min_y; dst_y_abs < max_y; dst_y_abs++ )
    {
        for( int dst_x_abs = min_x; dst_x_abs < max_x; dst_x_abs++ )
        {
            int rel_x = dst_x_abs - dst_x - dst_anchor_x;
            int rel_y = dst_y_abs - dst_y - dst_anchor_y;

            int src_rel_x = ((rel_x * cos + rel_y * sin) >> 16);
            int src_rel_y = ((-rel_x * sin + rel_y * cos) >> 16);

            int src_x = src_anchor_x + src_rel_x;
            int src_y = src_anchor_y + src_rel_y;

            if( src_x >= 0 && src_x < src_width && src_y >= 0 && src_y < src_height )
            {
                int src_pixel = src_buffer[src_y * src_width + src_x];
                if( src_pixel != 0 )
                    dst_buffer[dst_y_abs * dst_stride + dst_x_abs] = src_pixel;
            }
        }
    }
}

static void
render_imgui(
    struct Platform2_Emscripten_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game)
{
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 260), ImGuiCond_FirstUseEver);

    ImGui::Begin("Info");
    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate,
        ImGui::GetIO().Framerate);

    {
        struct PlatformMemoryInfo mem = {};
        if( platform_get_memory_info(&mem) )
        {
            float used_mb = mem.heap_used / (1024.0f * 1024.0f);
            float total_mb = mem.heap_total / (1024.0f * 1024.0f);
            ImGui::Text("Heap: %.1f / %.1f MB", used_mb, total_mb);
            if( mem.heap_total > 0 )
                ImGui::ProgressBar(
                    (float)mem.heap_used / (float)mem.heap_total, ImVec2(-1, 0), nullptr);
            if( mem.heap_peak > 0 )
                ImGui::Text("Peak: %.1f MB", mem.heap_peak / (1024.0f * 1024.0f));
        }
    }

    ImGui::Text("Max paint command: %d", game->cc);
    ImGui::Text("Trap command: %d", g_trap_command);
    if( ImGui::Button("Trap command") )
    {
        if( g_trap_command == -1 )
            g_trap_command = game->cc;
        else
            g_trap_command = -1;
    }

    ImGui::InputInt("Trap X", &g_trap_x);
    ImGui::InputInt("Trap Z", &g_trap_z);

    if( game->view_port )
    {
        ImGui::Separator();
        int w = game->view_port->width;
        int h = game->view_port->height;
        bool changed = ImGui::InputInt("World viewport W", &w);
        changed |= ImGui::InputInt("World viewport H", &h);
        if( changed )
        {
            if( w > renderer->max_width )
                w = renderer->max_width;
            if( h > renderer->max_height )
                h = renderer->max_height;
            LibToriRS_GameSetWorldViewportSize(game, w, h);
        }
    }

    ImGui::Text("Mouse (game x, y): %d, %d", game->mouse_x, game->mouse_y);

    if( renderer->platform && renderer->platform->window )
    {
        int window_mouse_x = 0;
        int window_mouse_y = 0;
        SDL_GetMouseState(&window_mouse_x, &window_mouse_y);
        ImGui::Text("Mouse (window x, y): %d, %d", window_mouse_x, window_mouse_y);
    }

    char camera_pos_text[256];
    snprintf(
        camera_pos_text,
        sizeof(camera_pos_text),
        "Camera (x, y, z): %d, %d, %d : %d, %d",
        game->camera_world_x,
        game->camera_world_y,
        game->camera_world_z,
        game->camera_world_x / 128,
        game->camera_world_z / 128);
    ImGui::Text("%s", camera_pos_text);
    ImGui::SameLine();
    if( ImGui::SmallButton("Copy##pos") )
    {
        ImGui::SetClipboardText(camera_pos_text);
    }

    char camera_rot_text[256];
    snprintf(
        camera_rot_text,
        sizeof(camera_rot_text),
        "Camera (pitch, yaw, roll): %d, %d, %d",
        game->camera_pitch,
        game->camera_yaw,
        game->camera_roll);
    ImGui::Text("%s", camera_rot_text);
    ImGui::SameLine();
    if( ImGui::SmallButton("Copy##rot") )
    {
        ImGui::SetClipboardText(camera_rot_text);
    }

    ImGui::Separator();
    ImGui::Checkbox("Show collision map", &g_show_collision_map);

    ImGui::Separator();
    ImGui::Text("Interface System:");
    ImGui::Text("Current viewport ID: %d", game->viewport_interface_id);
    ImGui::Text("Current sidebar ID: %d", game->sidebar_interface_id);
    ImGui::Text("Selected tab: %d", game->selected_tab);
    if( game->selected_tab >= 0 && game->selected_tab < 14 )
    {
        ImGui::Text(
            "Tab %d interface ID: %d",
            game->selected_tab,
            game->tab_interface_id[game->selected_tab]);
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer->renderer);
}

struct Platform2_Emscripten_SDL2_Renderer_Soft3D*
PlatformImpl2_Emscripten_SDL2_Renderer_Soft3D_New(
    int width,
    int height,
    int max_width,
    int max_height)
{
    struct Platform2_Emscripten_SDL2_Renderer_Soft3D* renderer =
        (struct Platform2_Emscripten_SDL2_Renderer_Soft3D*)malloc(
            sizeof(struct Platform2_Emscripten_SDL2_Renderer_Soft3D));
    memset(renderer, 0, sizeof(struct Platform2_Emscripten_SDL2_Renderer_Soft3D));

    renderer->width = width;
    renderer->height = height;
    renderer->max_width = max_width;
    renderer->max_height = max_height;

    renderer->pixel_buffer = (int*)malloc(max_width * max_height * sizeof(int));
    if( !renderer->pixel_buffer )
    {
        printf("Failed to allocate pixel buffer\n");
        return NULL;
    }
    memset(renderer->pixel_buffer, 0, width * height * sizeof(int));

    return renderer;
}

void
PlatformImpl2_Emscripten_SDL2_Renderer_Soft3D_Free(
    struct Platform2_Emscripten_SDL2_Renderer_Soft3D* renderer)
{
    free(renderer->dash_buffer);
    free(renderer->pixel_buffer);
    free(renderer);
}

bool
PlatformImpl2_Emscripten_SDL2_Renderer_Soft3D_Init(
    struct Platform2_Emscripten_SDL2_Renderer_Soft3D* renderer,
    struct Platform2_Emscripten_SDL2* platform)
{
    if( !renderer || !platform )
        return false;

    renderer->platform = platform;

    renderer->renderer = SDL_CreateRenderer(
        platform->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if( !renderer->renderer )
    {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    renderer->texture = SDL_CreateTexture(
        renderer->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        renderer->max_width,
        renderer->max_height);
    if( !renderer->texture )
    {
        printf("SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    // Initialize ImGui (same panel as OSX soft3d renderer)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    if( !ImGui_ImplSDL2_InitForSDLRenderer(platform->window, renderer->renderer) )
    {
        printf("ImGui SDL2 init failed\n");
        return false;
    }
    if( !ImGui_ImplSDLRenderer2_Init(renderer->renderer) )
    {
        printf("ImGui SDLRenderer init failed\n");
        return false;
    }

    return true;
}

void
PlatformImpl2_Emscripten_SDL2_Renderer_Soft3D_Render(
    struct Platform2_Emscripten_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    /* Match OSX soft3d: 3D rasterizes to dash_buffer (world viewport size); 2D UI uses
     * iface_view_port + pixel_buffer (full window stride). Composite 3D into pixel_buffer at
     * viewport_offset_* so UI coordinates stay in window space without shear. */
    if( game && game->view_port )
    {
        game->view_port->x_center = game->view_port->width / 2;
        game->view_port->y_center = game->view_port->height / 2;

        if( !renderer->dash_buffer || renderer->dash_buffer_width != game->view_port->width ||
            renderer->dash_buffer_height != game->view_port->height )
        {
            free(renderer->dash_buffer);
            renderer->dash_buffer_width = game->view_port->width;
            renderer->dash_buffer_height = game->view_port->height;
            if( renderer->dash_buffer_width > renderer->max_width ||
                renderer->dash_buffer_height > renderer->max_height )
            {
                renderer->dash_buffer = NULL;
            }
            else
            {
                renderer->dash_buffer = (int*)malloc(
                    (size_t)renderer->dash_buffer_width * (size_t)renderer->dash_buffer_height *
                    sizeof(int));
            }
            if( !renderer->dash_buffer )
            {
                printf("Failed to allocate dash buffer\n");
            }
            else
            {
                game->view_port->stride = renderer->dash_buffer_width;
            }
        }
    }

    if( renderer->dash_buffer )
    {
        for( int y = 0; y < renderer->dash_buffer_height; y++ )
            memset(
                &renderer->dash_buffer[y * renderer->dash_buffer_width],
                0,
                (size_t)renderer->dash_buffer_width * sizeof(int));
    }

    memset(
        renderer->pixel_buffer,
        0,
        (size_t)renderer->max_width * (size_t)renderer->max_height * sizeof(int));

    static std::vector<ToriRSRenderCommand> deferred_font_draws;
    deferred_font_draws.clear();

    struct ToriRSRenderCommand command;
    LibToriRS_FrameBegin(game, render_command_buffer);
    assert(game && render_command_buffer && renderer);
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &command, true) )
    {
        switch( command.kind )
        {
        case TORIRS_GFX_FONT_LOAD:
        case TORIRS_GFX_MODEL_LOAD:
        case TORIRS_GFX_TEXTURE_LOAD:
            break;
        case TORIRS_GFX_FONT_DRAW:
            deferred_font_draws.push_back(command);
            break;
        case TORIRS_GFX_MODEL_DRAW:
            if( renderer->dash_buffer )
            {
                dash3d_raster_projected_model(
                    game->sys_dash,
                    command._model_draw.model,
                    &command._model_draw.position,
                    game->view_port,
                    game->camera,
                    renderer->dash_buffer,
                    false);
            }
            break;
        case TORIRS_GFX_SPRITE_DRAW:
        {
            struct DashSprite* sp = command._sprite_draw.sprite;
            if( !sp || !game->sys_dash || !game->iface_view_port || !renderer->pixel_buffer )
                break;
            int iface_stride = game->iface_view_port->stride;
            int x = command._sprite_draw.x;
            int y = command._sprite_draw.y;
            int rot = command._sprite_draw.rotation_r2pi2048;
            if( rot != 0 )
            {
                blit_rotated_buffer(
                    (int*)sp->pixels_argb,
                    sp->width,
                    sp->height,
                    sp->width >> 1,
                    sp->height >> 1,
                    renderer->pixel_buffer,
                    iface_stride,
                    x + sp->crop_x,
                    y + sp->crop_y,
                    sp->width,
                    sp->height,
                    sp->width / 2,
                    sp->height / 2,
                    rot);
            }
            else
            {
                dash2d_blit_sprite(
                    game->sys_dash, sp, game->iface_view_port, x, y, renderer->pixel_buffer);
            }
        }
        break;
        default:
            break;
        }
    }
    LibToriRS_FrameEnd(game);

    if( renderer->dash_buffer && game->iface_view_port )
    {
        int* dash_buf = renderer->dash_buffer;
        int dash_w = renderer->dash_buffer_width;
        int dash_h = renderer->dash_buffer_height;
        int off_x = game->viewport_offset_x;
        int off_y = game->viewport_offset_y;
        int* pix = renderer->pixel_buffer;
        int pix_stride = game->iface_view_port->stride;
        int clip_w = game->iface_view_port->width;
        int clip_h = game->iface_view_port->height;
        for( int y = 0; y < dash_h; y++ )
        {
            int dst_y = y + off_y;
            if( dst_y < 0 || dst_y >= clip_h )
                continue;
            for( int x = 0; x < dash_w; x++ )
            {
                int dst_x = x + off_x;
                if( dst_x >= 0 && dst_x < clip_w )
                    pix[dst_y * pix_stride + dst_x] = dash_buf[y * dash_w + x];
            }
        }
    }

    /* Draw deferred font commands on top of the blitted 3D scene. */
    for( const auto& fc : deferred_font_draws )
    {
        struct DashPixFont* f = fc._font_draw.font;
        if( f && fc._font_draw.text && renderer->pixel_buffer && game->iface_view_port )
            dashfont_draw_text_ex(
                f,
                (uint8_t*)fc._font_draw.text,
                fc._font_draw.x,
                fc._font_draw.y,
                fc._font_draw.color_rgb,
                renderer->pixel_buffer,
                game->iface_view_port->stride);
    }

    int upload_w = renderer->width;
    int upload_h = renderer->height;
    if( game && game->iface_view_port )
    {
        upload_w = game->iface_view_port->width;
        upload_h = game->iface_view_port->height;
    }
    const int src_pitch_px =
        game && game->iface_view_port ? game->iface_view_port->stride : renderer->width;
    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        renderer->pixel_buffer,
        upload_w,
        upload_h,
        32,
        src_pitch_px * (int)sizeof(int),
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000);

    // Copy the pixels into the texture, taking SDL pitch into account
    int* pix_write = NULL;
    int texture_pitch = 0;
    if( SDL_LockTexture(renderer->texture, NULL, (void**)&pix_write, &texture_pitch) < 0 )
    {
        SDL_FreeSurface(surface);
        return;
    }

    int texture_w = texture_pitch / sizeof(int); // Convert pitch (bytes) to pixels
    int* src_pixels = (int*)surface->pixels;

    for( int src_y = 0; src_y < upload_h; src_y++ )
    {
        int* dst_row = &pix_write[src_y * texture_w];
        int* src_row = &src_pixels[src_y * src_pitch_px];
        memcpy(dst_row, src_row, (size_t)upload_w * sizeof(int));
    }

    // Unlock the texture so that it may be used elsewhere
    SDL_UnlockTexture(renderer->texture);

    // Clear renderer and draw the output scaled to the canvas with aspect-ratio preservation.
    SDL_SetRenderDrawColor(renderer->renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer->renderer);

    int dst_w = 0;
    int dst_h = 0;
    if( renderer->platform && renderer->platform->window )
        SDL_GetWindowSize(renderer->platform->window, &dst_w, &dst_h);
    if( dst_w <= 0 || dst_h <= 0 )
        SDL_GetRendererOutputSize(renderer->renderer, &dst_w, &dst_h);

    /* Letterbox: fit the fixed output (upload_w × upload_h, in output space) into the
     * canvas (dst_w × dst_h) while preserving the output's aspect ratio.
     * All viewport values are in output space, so this scales everything together. */
    SDL_Rect dst_rect = { 0, 0, dst_w, dst_h };
    if( upload_w > 0 && upload_h > 0 && dst_w > 0 && dst_h > 0 )
    {
        const float ca = (float)dst_w / (float)dst_h;
        const float ga = (float)upload_w / (float)upload_h;
        int lx, ly, lw, lh;
        if( ga > ca )
        {
            lw = dst_w;
            lh = (int)((float)dst_w / ga);
            lx = 0;
            ly = (dst_h - lh) / 2;
        }
        else
        {
            lh = dst_h;
            lw = (int)((float)dst_h * ga);
            ly = 0;
            lx = (dst_w - lw) / 2;
        }
        dst_rect = { lx, ly, lw, lh };
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // Nearest
    SDL_Rect src_rect = { 0, 0, upload_w, upload_h };
    SDL_RenderCopy(renderer->renderer, renderer->texture, &src_rect, &dst_rect);
    SDL_FreeSurface(surface);

    render_imgui(renderer, game);

    SDL_RenderPresent(renderer->renderer);
}