

#include "platform_impl2_emscripten_sdl2_renderer_soft3d.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

extern "C" {
#include "osrs/game.h"
#include "tori_rs.h"
}

extern int g_trap_command;
extern int g_trap_x;
extern int g_trap_z;

static bool g_show_collision_map = false;

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
    int vw = renderer->width;
    int vh = renderer->height;
    if( game && game->view_port )
    {
        vw = game->view_port->width;
        vh = game->view_port->height;
        if( vw > renderer->max_width )
            vw = renderer->max_width;
        if( vh > renderer->max_height )
            vh = renderer->max_height;
    }

    memset(
        renderer->pixel_buffer,
        0,
        (size_t)renderer->max_width * (size_t)renderer->max_height * sizeof(int));

    struct ToriRSRenderCommand command;
    LibToriRS_FrameBegin(game, render_command_buffer);
    assert(game && render_command_buffer && renderer);
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &command, true) )
    {
        switch( command.kind )
        {
        case TORIRS_GFX_MODEL_LOAD:
        case TORIRS_GFX_TEXTURE_LOAD:
            break;
        case TORIRS_GFX_MODEL_DRAW:
            dash3d_raster_projected_model(
                game->sys_dash,
                command._model_draw.model,
                &command._model_draw.position,
                game->view_port,
                game->camera,
                renderer->pixel_buffer,
                false);
            break;
        default:
            break;
        }
    }
    LibToriRS_FrameEnd(game);

    // Render minimap to buffer starting at (0,0)
    // Calculate the center of the minimap content for rotation anchor

    const int src_pitch_px = game && game->view_port ? game->view_port->stride : vw;
    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        renderer->pixel_buffer,
        vw,
        vh,
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

    for( int src_y = 0; src_y < vh; src_y++ )
    {
        int* dst_row = &pix_write[src_y * texture_w];
        int* src_row = &src_pixels[src_y * src_pitch_px];
        memcpy(dst_row, src_row, (size_t)vw * sizeof(int));
    }

    // Unlock the texture so that it may be used elsewhere
    SDL_UnlockTexture(renderer->texture);

    // Clear renderer and draw the texture with aspect-ratio–preserving scaling
    SDL_SetRenderDrawColor(renderer->renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer->renderer);

    int dst_w = 0;
    int dst_h = 0;
    if( renderer->platform && renderer->platform->window )
        SDL_GetWindowSize(renderer->platform->window, &dst_w, &dst_h);
    if( dst_w <= 0 || dst_h <= 0 )
        SDL_GetRendererOutputSize(renderer->renderer, &dst_w, &dst_h);

    SDL_Rect dst_rect = { 0, 0, dst_w, dst_h };
    if( game && game->view_port )
    {
        int x = game->viewport_offset_x;
        int y = game->viewport_offset_y;
        int w = game->view_port->width;
        int h = game->view_port->height;

        if( x < 0 )
            x = 0;
        if( y < 0 )
            y = 0;
        if( x + w > dst_w )
            w = dst_w - x;
        if( y + h > dst_h )
            h = dst_h - y;
        if( w > 0 && h > 0 )
            dst_rect = { x, y, w, h };
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // Nearest
    SDL_Rect src_rect = { 0, 0, vw, vh };
    SDL_RenderCopy(renderer->renderer, renderer->texture, &src_rect, &dst_rect);
    SDL_FreeSurface(surface);

    render_imgui(renderer, game);

    SDL_RenderPresent(renderer->renderer);
}