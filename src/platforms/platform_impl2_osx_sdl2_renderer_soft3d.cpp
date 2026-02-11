#include "platform_impl2_osx_sdl2_renderer_soft3d.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

extern "C" {
#include "3rd/lua/lauxlib.h"
#include "3rd/lua/lua.h"
#include "3rd/lua/lualib.h"
#include "graphics/convex_hull.u.c"
#include "graphics/dash.h"
#include "osrs/_light_model_default.u.c"
#include "osrs/buildcachedat.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/collision_map.h"
#include "osrs/colors.h"
#include "osrs/dash_utils.h"
#include "osrs/game_entity.h"
#include "osrs/gio.h"
#include "osrs/gio_cache_dat.h"
#include "osrs/interface.h"
#include "osrs/minimap.h"
#include "osrs/minimenu.h"
#include "osrs/model_transforms.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables_dat/pixfont.h"
#include "osrs/scene.h"
#include "osrs/script_queue.h"
#include "server/prot.h"
#include "server/server.h"
#include "tori_rs.h"
}

#define LUA_SCRIPTS_DIR "/Users/matthewevers/Documents/git_repos/3draster/src/osrs/scripts"

#include <SDL.h>
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_trap_command;
extern int g_trap_x;
extern int g_trap_z;

static bool g_show_collision_map = false;

static void
render_imgui(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game)
{
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Info window
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);

    ImGui::Begin("Info");
    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate,
        ImGui::GetIO().Framerate);
    Uint64 frequency = SDL_GetPerformanceFrequency();

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

    // ImGui::Text(
    //     "Render Time: %.3f ms/frame",
    //     (double)(game->end_time - game->start_time) * 1000.0 / (double)frequency);
    // ImGui::Text(
    //     "Average Render Time: %.3f ms/frame, %.3f ms/frame, %.3f ms/frame",
    //     (double)(game->frame_time_sum / game->frame_count) * 1000.0 / (double)frequency,
    //     (double)(game->painters_time_sum / game->frame_count) * 1000.0 / (double)frequency,
    //     (double)(game->texture_upload_time_sum / game->frame_count) * 1000.0 /
    //     (double)frequency);

    // Display mouse position (game screen coordinates)
    ImGui::Text("Mouse (game x, y): %d, %d", game->mouse_x, game->mouse_y);

    if( game->hovered_scene_element )
    {
        struct SceneElement* el = game->hovered_scene_element;
        if( el->config_loc_id >= 0 )
            ImGui::Text("Hover config_loc_id: %d (tile %d, %d)", el->config_loc_id, el->tile_sx, el->tile_sz);
        else
            ImGui::Text("Hover (no config_loc_id) (tile %d, %d)", el->tile_sx, el->tile_sz);
    }

    // Also show window mouse position for debugging
    if( renderer->platform && renderer->platform->window )
    {
        int window_mouse_x, window_mouse_y;
        SDL_GetMouseState(&window_mouse_x, &window_mouse_y);
        ImGui::Text("Mouse (window x, y): %d, %d", window_mouse_x, window_mouse_y);
    }

    // ImGui::Text("Hover model: %d, %d", game->hover_model, game->hover_loc_yaw);
    // ImGui::Text(
    //     "Hover loc: %d, %d, %d", game->hover_loc_x, game->hover_loc_y, game->hover_loc_level);

    // Camera position with copy button
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

    // Camera rotation with copy button
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

    // Display clicked tile information
    ImGui::Separator();
    if( renderer->clicked_tile_x != -1 && renderer->clicked_tile_z != -1 )
    {
        char clicked_tile_text[256];
        snprintf(
            clicked_tile_text,
            sizeof(clicked_tile_text),
            "Clicked Tile: (%d, %d, level %d)",
            renderer->clicked_tile_x,
            renderer->clicked_tile_z,
            renderer->clicked_tile_level);
        ImGui::Text("%s", clicked_tile_text);
        ImGui::SameLine();
        if( ImGui::SmallButton("Copy##tile") )
        {
            ImGui::SetClipboardText(clicked_tile_text);
        }
    }
    else
    {
        ImGui::Text("Clicked Tile: None");
    }

    // Add camera speed slider
    ImGui::Separator();

    ImGui::Separator();
    ImGui::Checkbox("Show collision map", &g_show_collision_map);

    // Interface controls removed - now server-controlled via IF_SETTAB/IF_SETTAB_ACTIVE packets
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

struct Platform2_OSX_SDL2_Renderer_Soft3D*
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_New(
    int width,
    int height,
    int max_width,
    int max_height)
{
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer =
        (struct Platform2_OSX_SDL2_Renderer_Soft3D*)malloc(
            sizeof(struct Platform2_OSX_SDL2_Renderer_Soft3D));
    memset(renderer, 0, sizeof(struct Platform2_OSX_SDL2_Renderer_Soft3D));

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

    // Initialize dash buffer (will be allocated when viewport is known)
    renderer->dash_buffer = NULL;
    renderer->dash_buffer_width = 0;
    renderer->dash_buffer_height = 0;
    renderer->dash_offset_x = 0;
    renderer->dash_offset_y = 0;
    renderer->highlight_poly_valid = 0;

    // Initialize minimap buffer (will be allocated when needed)
    renderer->minimap_buffer = NULL;
    renderer->minimap_buffer_width = 0;
    renderer->minimap_buffer_height = 0;

    // Initialize outgoing message buffer
    renderer->outgoing_message_size = 0;

    // Initialize client-side position interpolation
    renderer->client_player_pos_tile_x = 0;
    renderer->client_player_pos_tile_z = 0;
    renderer->client_target_tile_x = 0;
    renderer->client_target_tile_z = 0;
    renderer->last_move_time_ms = 0;

    return renderer;
}

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Free(struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer)
{
    if( renderer->dash_buffer )
    {
        free(renderer->dash_buffer);
    }
    if( renderer->minimap_buffer )
    {
        free(renderer->minimap_buffer);
    }
    free(renderer);
}

bool
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Init(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct Platform2_OSX_SDL2* platform)
{
    renderer->platform = platform;

    renderer->renderer = SDL_CreateRenderer(platform->window, -1, SDL_RENDERER_ACCELERATED);
    if( !renderer->renderer )
    {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        return false;
    }

    renderer->texture = SDL_CreateTexture(
        renderer->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        renderer->width,
        renderer->height);
    if( !renderer->texture )
    {
        printf("Failed to create texture\n");
        return false;
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    if( !ImGui_ImplSDL2_InitForSDLRenderer(platform->window, renderer->renderer) )
    {
        printf("ImGui SDL2 init failed\n");
        return false;
    }
    if( !ImGui_ImplSDLRenderer2_Init(renderer->renderer) )
    {
        printf("ImGui Renderer init failed\n");
        return false;
    }

    return true;
}

// Forward declarations for example interface functions
static void
create_example_interface(struct GGame* game);
static void
init_example_interface(struct GGame* game);

// Public function to initialize the example interface after game is loaded
void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_InitExampleInterface(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game)
{
    printf("\n=== Initializing Example Interface ===\n");
    init_example_interface(game);
    printf("=====================================\n\n");
}

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Shutdown(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer);

// Helper function to create a test interface for demonstration
static void
create_example_interface(struct GGame* game)
{
    printf("Creating example interface components...\n");

    // Create root layer component (ID: 10000)
    struct CacheDatConfigComponent* root =
        (struct CacheDatConfigComponent*)malloc(sizeof(struct CacheDatConfigComponent));
    memset(root, 0, sizeof(struct CacheDatConfigComponent));

    root->id = 10000;
    root->layer = -1;
    root->type = COMPONENT_TYPE_LAYER;
    root->buttonType = -1;
    root->clientCode = 0;
    root->width = 400;
    root->height = 300;
    root->x = 0;
    root->y = 0;
    root->alpha = 0;
    root->hide = false;
    root->scroll = 0;

    // This layer has 4 children: background, title, description, and icon
    root->children_count = 4;
    root->children = (int*)malloc(4 * sizeof(int));
    root->childX = (int*)malloc(4 * sizeof(int));
    root->childY = (int*)malloc(4 * sizeof(int));

    root->children[0] = 10001; // Background
    root->childX[0] = 50;
    root->childY[0] = 50;

    root->children[1] = 10002; // Title
    root->childX[1] = 60;
    root->childY[1] = 60;

    root->children[2] = 10003; // Description
    root->childX[2] = 60;
    root->childY[2] = 90;

    root->children[3] = 10004; // Icon
    root->childX[3] = 350;
    root->childY[3] = 60;

    buildcachedat_add_component(game->buildcachedat, 10000, root);

    // Child 0: Semi-transparent background rectangle (ID: 10001)
    struct CacheDatConfigComponent* bg =
        (struct CacheDatConfigComponent*)malloc(sizeof(struct CacheDatConfigComponent));
    memset(bg, 0, sizeof(struct CacheDatConfigComponent));

    bg->id = 10001;
    bg->layer = 10000;
    bg->type = COMPONENT_TYPE_RECT;
    bg->buttonType = -1;
    bg->width = 400;
    bg->height = 300;
    bg->x = 0;
    bg->y = 0;
    bg->fill = true;
    bg->colour = 0x2C1810; // Dark brown
    bg->alpha = 180;       // Semi-transparent

    buildcachedat_add_component(game->buildcachedat, 10001, bg);

    // Child 1: Title text (ID: 10002)
    struct CacheDatConfigComponent* title =
        (struct CacheDatConfigComponent*)malloc(sizeof(struct CacheDatConfigComponent));
    memset(title, 0, sizeof(struct CacheDatConfigComponent));

    title->id = 10002;
    title->layer = 10000;
    title->type = COMPONENT_TYPE_TEXT;
    title->buttonType = -1;
    title->width = 380;
    title->height = 20;
    title->x = 0;
    title->y = 0;
    title->font = 2; // Bold font
    title->text = strdup("Example Interface - 2D UI System");
    title->colour = 0xFFFF00; // Yellow
    title->center = false;
    title->shadowed = true;

    buildcachedat_add_component(game->buildcachedat, 10002, title);

    // Child 2: Description text (ID: 10003)
    struct CacheDatConfigComponent* desc =
        (struct CacheDatConfigComponent*)malloc(sizeof(struct CacheDatConfigComponent));
    memset(desc, 0, sizeof(struct CacheDatConfigComponent));

    desc->id = 10003;
    desc->layer = 10000;
    desc->type = COMPONENT_TYPE_TEXT;
    desc->buttonType = -1;
    desc->width = 380;
    desc->height = 200;
    desc->x = 0;
    desc->y = 0;
    desc->font = 1; // Regular font
    desc->text = strdup(
        "This is a test interface created programmatically.\\nIt demonstrates:  Layers  Rectangles "
        "with alpha  Text rendering  Sprite graphics\\nPress 'I' to toggle interface visibility.");
    desc->colour = 0xFFFFFF; // White
    desc->center = false;
    desc->shadowed = false;

    buildcachedat_add_component(game->buildcachedat, 10003, desc);

    // Child 3: Compass icon (ID: 10004)
    struct CacheDatConfigComponent* icon =
        (struct CacheDatConfigComponent*)malloc(sizeof(struct CacheDatConfigComponent));
    memset(icon, 0, sizeof(struct CacheDatConfigComponent));

    icon->id = 10004;
    icon->layer = 10000;
    icon->type = COMPONENT_TYPE_GRAPHIC;
    icon->buttonType = -1;
    icon->width = 33;
    icon->height = 33;
    icon->x = 0;
    icon->y = 0;
    icon->graphic = strdup("example_compass");

    buildcachedat_add_component(game->buildcachedat, 10004, icon);

    // Register the compass sprite for the icon
    if( game->sprite_compass )
    {
        printf("  Registering compass sprite for interface...\n");
        buildcachedat_add_component_sprite(
            game->buildcachedat, "example_compass", game->sprite_compass);
        printf("  Compass sprite registered successfully\n");
    }
    else
    {
        printf("  Warning: compass sprite not loaded yet\n");
    }

    printf("Example interface created with 5 components (IDs: 10000-10004)\n");
    printf("  - Layer container (10000)\n");
    printf("  - Background rectangle (10001)\n");
    printf("  - Title text (10002)\n");
    printf("  - Description text (10003)\n");
    printf("  - Compass icon (10004)\n");
}

// Create an example inventory interface
static void
create_example_inventory(struct GGame* game)
{
    printf("Creating example inventory interface...\n");

    // Parent Layer (ID: 10100) - positioned at 0,0 relative to sidebar (553, 205)
    struct CacheDatConfigComponent* inv_layer =
        (struct CacheDatConfigComponent*)malloc(sizeof(struct CacheDatConfigComponent));
    memset(inv_layer, 0, sizeof(struct CacheDatConfigComponent));

    inv_layer->id = 10100;
    inv_layer->type = COMPONENT_TYPE_LAYER;
    inv_layer->layer = -1;
    inv_layer->buttonType = -1;
    inv_layer->width = 190;  // Sidebar width
    inv_layer->height = 261; // Sidebar height (466-205)
    inv_layer->x = 0;
    inv_layer->y = 0;

    // Allocate children array (1 child: inventory grid, no background to see invback sprite)
    inv_layer->children_count = 1;
    inv_layer->children = (int*)malloc(sizeof(int) * 1);
    inv_layer->childX = (int*)malloc(sizeof(int) * 1);
    inv_layer->childY = (int*)malloc(sizeof(int) * 1);

    inv_layer->children[0] = 10102; // Inventory grid
    inv_layer->childX[0] = 10;
    inv_layer->childY[0] = 10;

    buildcachedat_add_component(game->buildcachedat, 10100, inv_layer);

    // Inventory component (ID: 10102)
    struct CacheDatConfigComponent* inventory =
        (struct CacheDatConfigComponent*)malloc(sizeof(struct CacheDatConfigComponent));
    memset(inventory, 0, sizeof(struct CacheDatConfigComponent));

    inventory->id = 10102;
    inventory->layer = 10100;
    inventory->type = COMPONENT_TYPE_INV;
    inventory->buttonType = -1;
    inventory->width = 4;  // 4 columns
    inventory->height = 7; // 7 rows (28 slots total)
    inventory->x = 0;
    inventory->y = 0;
    inventory->marginX = 5; // 5px horizontal margin between slots
    inventory->marginY = 5; // 5px vertical margin between slots

    // Allocate inventory slots (28 slots = 4x7)
    int total_slots = 28;
    inventory->invSlotObjId = (int*)malloc(sizeof(int) * total_slots);
    inventory->invSlotObjCount = (int*)malloc(sizeof(int) * total_slots);
    inventory->invSlotOffsetX = (int*)malloc(sizeof(int) * 20);
    inventory->invSlotOffsetY = (int*)malloc(sizeof(int) * 20);

    // Initialize all slots to empty
    for( int i = 0; i < total_slots; i++ )
    {
        inventory->invSlotObjId[i] = 0;
        inventory->invSlotObjCount[i] = 0;
    }

    // Initialize offsets to 0
    for( int i = 0; i < 20; i++ )
    {
        inventory->invSlotOffsetX[i] = 0;
        inventory->invSlotOffsetY[i] = 0;
    }

    // Add classic OSRS items that exist in old cache
    // Bones
    inventory->invSlotObjId[0] = 526;
    inventory->invSlotObjCount[0] = 1;

    // Coins
    inventory->invSlotObjId[1] = 995;
    inventory->invSlotObjCount[1] = 10000;

    // Logs
    inventory->invSlotObjId[2] = 1511;
    inventory->invSlotObjCount[2] = 15;

    // Ball of wool
    inventory->invSlotObjId[3] = 1759;
    inventory->invSlotObjCount[3] = 5;

    // Rune scimitar
    inventory->invSlotObjId[5] = 1333;
    inventory->invSlotObjCount[5] = 1;

    // Bronze sword
    inventory->invSlotObjId[10] = 1277;
    inventory->invSlotObjCount[10] = 1;

    // Iron ore
    inventory->invSlotObjId[15] = 440;
    inventory->invSlotObjCount[15] = 20;

    // Cooked meat
    inventory->invSlotObjId[20] = 2142;
    inventory->invSlotObjCount[20] = 4;

    buildcachedat_add_component(game->buildcachedat, 10102, inventory);

    printf("Example inventory created with ID 10100\n");
    printf("  - Inventory layer (10100)\n");
    printf("  - Inventory grid 4x7 (10102) with 8 items\n");
}

// Function to initialize the example interface (forward declaration)
static void
init_example_interface(struct GGame* game);

// Function to initialize the example interface
static void
init_example_interface(struct GGame* game)
{
    if( !game || !game->buildcachedat )
    {
        printf("ERROR: Cannot initialize example interface - game or buildcachedat is null\n");
        return;
    }

    printf("Initializing example interface...\n");

    // Queue Lua script to load object configs and models asynchronously
    printf("Queuing Lua script to load inventory models...\n");

    struct ScriptArgs args = { .tag = SCRIPT_LOAD_INVENTORY_MODELS,
                               .u.load_inventory_models = { .dummy = 0 } };
    script_queue_push(&game->script_queue, &args);

    printf("  Script queued: load_inventory_models.lua\n");
    printf(
        "  Note: Models will load asynchronously. Icons will generate once models are loaded.\n");

    // Create the example interface components (UI structure can be created immediately)
    create_example_interface(game);
    create_example_inventory(game);

    // Don't display it immediately - let user toggle with 'I' key
    game->viewport_interface_id = -1;

    printf("Example interface ready. Use ImGui buttons to toggle visibility.\n");
}

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_SetDashOffset(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    int offset_x,
    int offset_y)
{
    if( renderer )
    {
        renderer->dash_offset_x = offset_x;
        renderer->dash_offset_y = offset_y;
    }
}

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
    // if (max_y > dst_height)
    //     max_y = dst_height;

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

            // Check bounds
            if( src_x >= 0 && src_x < src_width && src_y >= 0 && src_y < src_height )
            {
                int src_pixel = src_buffer[src_y * src_width + src_x];
                if( src_pixel != 0 )
                {
                    dst_buffer[dst_y_abs * dst_stride + dst_x_abs] = src_pixel;
                }
            }
        }
    }
}

int
height_of_entity(
    struct GGame* game,
    struct SceneElement* element)
{
    struct DashModel* model = element->dash_model;
    if( model )
    {
        return model->bounds_cylinder->max_y - model->bounds_cylinder->min_y;
    }
    return 0;
}

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Render(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    /* Sync viewport offset so frame logic (menu, cross, hover) uses correct coords. */
    game->viewport_offset_x = renderer->dash_offset_x;
    game->viewport_offset_y = renderer->dash_offset_y;

    // Ensure viewport center matches viewport dimensions (critical for coordinate transformations)

    // Handle window resize: update renderer dimensions up to max size
    int window_width, window_height;
    SDL_GetWindowSize(renderer->platform->window, &window_width, &window_height);

    // Clamp to maximum renderer size (pixel buffer allocation limit)
    int new_width = window_width > renderer->max_width ? renderer->max_width : window_width;
    int new_height = window_height > renderer->max_height ? renderer->max_height : window_height;

    if( game->iface_view_port )
    {
        game->iface_view_port->x_center = new_width / 2;
        game->iface_view_port->y_center = new_height / 2;
        game->iface_view_port->width = new_width;
        game->iface_view_port->height = new_height;
    }

    // Allocate/update dash buffer if viewport exists
    if( game->view_port )
    {
        // Ensure viewport center matches viewport dimensions (not renderer dimensions)
        // This is critical for coordinate transformations to work correctly
        game->view_port->x_center = game->view_port->width / 2;
        game->view_port->y_center = game->view_port->height / 2;

        // Allocate or reallocate dash buffer if size changed
        if( !renderer->dash_buffer || renderer->dash_buffer_width != game->view_port->width ||
            renderer->dash_buffer_height != game->view_port->height )
        {
            if( renderer->dash_buffer )
            {
                free(renderer->dash_buffer);
            }

            renderer->dash_buffer_width = game->view_port->width;
            renderer->dash_buffer_height = game->view_port->height;
            renderer->dash_buffer = (int*)malloc(
                renderer->dash_buffer_width * renderer->dash_buffer_height * sizeof(int));
            if( !renderer->dash_buffer )
            {
                printf("Failed to allocate dash buffer\n");
                return;
            }

            // Set stride to dash buffer width for dash rendering
            game->view_port->stride = renderer->dash_buffer_width;
        }
    }

    // Only update if size changed
    if( new_width != renderer->width || new_height != renderer->height )
    {
        // renderer->width = new_width;
        // renderer->height = new_height;

        // Recreate texture with new dimensions
        if( renderer->texture )
        {
            SDL_DestroyTexture(renderer->texture);
        }

        renderer->texture = SDL_CreateTexture(
            renderer->renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            renderer->width,
            renderer->height);

        if( !renderer->texture )
        {
            printf(
                "Failed to recreate texture for new size %dx%d\n",
                renderer->width,
                renderer->height);
        }
        else
        {
        }
    }

    // Clear main pixel buffer

    for( int y = 0; y < renderer->height; y++ )
        memset(&renderer->pixel_buffer[y * renderer->width], 0x00, renderer->width * sizeof(int));

    // Clear dash buffer if it exists
    if( renderer->dash_buffer )
    {
        for( int y = 0; y < renderer->dash_buffer_height; y++ )
            memset(
                &renderer->dash_buffer[y * renderer->dash_buffer_width],
                0x00,
                renderer->dash_buffer_width * sizeof(int));
    }

    // Allocate minimap buffer if needed (size based on radius * 2 * 4 pixels per tile)
    int minimap_size = 220; // Slightly larger than 25*2*4 = 200 for safety
    if( !renderer->minimap_buffer || renderer->minimap_buffer_width != minimap_size ||
        renderer->minimap_buffer_height != minimap_size )
    {
        if( renderer->minimap_buffer )
        {
            free(renderer->minimap_buffer);
        }

        renderer->minimap_buffer_width = minimap_size;
        renderer->minimap_buffer_height = minimap_size;
        renderer->minimap_buffer = (int*)malloc(
            renderer->minimap_buffer_width * renderer->minimap_buffer_height * sizeof(int));
        if( !renderer->minimap_buffer )
        {
            printf("Failed to allocate minimap buffer\n");
            return;
        }
    }

    // Clear minimap buffer
    for( int y = 0; y < renderer->minimap_buffer_height; y++ )
        memset(
            &renderer->minimap_buffer[y * renderer->minimap_buffer_width],
            0x00,
            renderer->minimap_buffer_width * sizeof(int));

    // struct AABB aabb;
    struct ToriRSRenderCommand command = { 0 };

    renderer->highlight_poly_valid = 0;

    LibToriRS_FrameBegin(game, render_command_buffer);
    while( LibToriRS_FrameNextCommand(game, render_command_buffer, &command) )
    {
        switch( command.kind )
        {
        case TORIRS_GFX_MODEL_DRAW:
            dash3d_raster_projected_model(
                game->sys_dash,
                command._model_draw.model,
                &command._model_draw.position,
                game->view_port,
                game->camera,
                renderer->dash_buffer,
                false);
            break;
        case TORIRS_GFX_MODEL_DRAW_HIGHLIGHT:
        {
            struct DashModel* model = command._model_draw.model;
            struct DashPosition* position = &command._model_draw.position;
            if( game->sys_dash && model && model->vertex_count >= 3 && game->view_port &&
                game->camera )
            {
                static const int DASH_MAX_VERTICES = 4096;
                static float hull_in_x[DASH_MAX_VERTICES];
                static float hull_in_y[DASH_MAX_VERTICES];
                static float hull_out_x[DASH_MAX_VERTICES];
                static float hull_out_y[DASH_MAX_VERTICES];
                int n = model->vertex_count;
                if( n > DASH_MAX_VERTICES )
                    n = DASH_MAX_VERTICES;
                dash3d_copy_screen_vertices_float(game->sys_dash, hull_in_x, hull_in_y, n);
                size_t hull_n =
                    compute_convex_hull(hull_in_x, hull_in_y, (size_t)n, hull_out_x, hull_out_y);
                if( hull_n >= 3 && hull_n <= (size_t)PLATFORM_SOFT3D_HIGHLIGHT_POLY_MAX )
                {
                    int cx = game->view_port->x_center;
                    int cy = game->view_port->y_center;
                    for( size_t i = 0; i < hull_n; i++ )
                    {
                        renderer->highlight_poly_x[i] = (int)hull_out_x[i] + cx;
                        renderer->highlight_poly_y[i] = (int)hull_out_y[i] + cy;
                    }
                    renderer->highlight_poly_n = (int)hull_n;
                    renderer->highlight_poly_valid = 1;
                }
            }
            break;
        }
        default:
            break;
        }
    }
    LibToriRS_FrameEnd(game);

    /* Client.ts entityOverlays: draw health bars and hitsplats for entities with damage */
    if( renderer->dash_buffer && game->sys_dash && game->view_port && game->camera && game->scene &&
        game->scene->terrain && game->pixfont_p12 )
    {
        dash2d_set_bounds(
            game->view_port, 0, 0, renderer->dash_buffer_width, renderer->dash_buffer_height);
        int* db = renderer->dash_buffer;
        int stride = renderer->dash_buffer_width;
        int clip_l = 0;
        int clip_t = 0;
        int clip_r = renderer->dash_buffer_width;
        int clip_b = renderer->dash_buffer_height;

        struct SceneTerrain* terrain = game->scene->terrain;

        auto draw_entity_overlays = [&](void* scene_element_ptr,
                                        int* damage_values,
                                        int* damage_types,
                                        int* damage_cycles,
                                        int combat_cycle,
                                        int health,
                                        int total_health) {
            int scene_local_x = ((struct SceneElement*)scene_element_ptr)->dash_position->x;
            int scene_local_z = ((struct SceneElement*)scene_element_ptr)->dash_position->z;
            int scene_local_height = ((struct SceneElement*)scene_element_ptr)->dash_position->y;
            scene_local_x -= game->camera_world_x;
            scene_local_z -= game->camera_world_z;
            scene_local_height -= game->camera_world_y;
            /* Entity height from dash_model bounds_cylinder (Client.ts entity.height) */
            int entity_height = height_of_entity(game, (struct SceneElement*)scene_element_ptr);

            /* World position for projection */

            int screen_x, screen_y;

            /* Health bar: Client.ts getOverlayPosEntity(entity, entity.height + 15) */
            if( combat_cycle > game->cycle + 100 && total_health > 0 )
            {
                int bar_y_world = scene_local_height - (entity_height - 15);
                int rel_y = bar_y_world;
                if( dash3d_project_point(
                        game->sys_dash,
                        scene_local_x,
                        rel_y,
                        scene_local_z,
                        game->view_port,
                        game->camera,
                        &screen_x,
                        &screen_y) )
                {
                    int bar_w = (health * 30) / total_health;
                    if( bar_w > 30 )
                        bar_w = 30;
                    dash2d_fill_rect_clipped(
                        db,
                        stride,
                        screen_x - 15,
                        screen_y - 3,
                        bar_w,
                        5,
                        GREEN,
                        clip_l,
                        clip_t,
                        clip_r,
                        clip_b);
                    dash2d_fill_rect_clipped(
                        db,
                        stride,
                        screen_x - 15 + bar_w,
                        screen_y - 3,
                        30 - bar_w,
                        5,
                        RED,
                        clip_l,
                        clip_t,
                        clip_r,
                        clip_b);
                }
            }

            /* Hitsplats: Client.ts getOverlayPosEntity(entity, entity.height / 2) */
            for( int i = 0; i < ENTITY_DAMAGE_SLOTS; i++ )
            {
                if( damage_cycles[i] <= game->cycle )
                    continue;

                int splat_y_world = scene_local_height - (entity_height / 2);
                int rel_y = splat_y_world;
                if( !dash3d_project_point(
                        game->sys_dash,
                        scene_local_x,
                        rel_y,
                        scene_local_z,
                        game->view_port,
                        game->camera,
                        &screen_x,
                        &screen_y) )
                    continue;

                int px = screen_x;
                int py = screen_y;
                if( i == 1 )
                    py -= 20;
                else if( i == 2 )
                {
                    px -= 15;
                    py -= 10;
                }
                else if( i == 3 )
                {
                    px += 15;
                    py -= 10;
                }

                int type = damage_types[i];
                if( type >= 0 && type < 20 && game->sprite_hitmarks[type] )
                {
                    dash2d_blit_sprite_alpha(
                        game->sys_dash,
                        game->sprite_hitmarks[type],
                        game->view_port,
                        px - 12,
                        py - 12,
                        255,
                        db);
                }
                char buf[16];
                int len = snprintf(buf, sizeof(buf), "%d", damage_values[i]);
                if( len > 0 )
                {
                    int w = dashfont_text_width(game->pixfont_p11, (uint8_t*)buf);

                    dashfont_draw_text_clipped(
                        game->pixfont_p11,
                        (uint8_t*)buf,
                        px - w / 2,
                        py - 9 + 4,
                        BLACK,
                        db,
                        stride,
                        clip_l,
                        clip_t,
                        clip_r,
                        clip_b);
                    dashfont_draw_text_clipped(
                        game->pixfont_p11,
                        (uint8_t*)buf,
                        px - w / 2 - 1,
                        py - 9 + 3,
                        WHITE,
                        db,
                        stride,
                        clip_l,
                        clip_t,
                        clip_r,
                        clip_b);
                }
            }
        };

        /* Local player */
        {
            struct PlayerEntity* pl = &game->players[ACTIVE_PLAYER_SLOT];
            if( pl->alive )
                draw_entity_overlays(
                    pl->scene_element,
                    pl->damage_values,
                    pl->damage_types,
                    pl->damage_cycles,
                    pl->combat_cycle,
                    pl->health,
                    pl->total_health);
        }
        /* Other players */
        for( int i = 0; i < game->player_count; i++ )
        {
            int pid = game->active_players[i];
            if( pid == ACTIVE_PLAYER_SLOT )
                continue;
            struct PlayerEntity* pl = &game->players[pid];
            if( pl->alive )
                draw_entity_overlays(
                    pl->scene_element,
                    pl->damage_values,
                    pl->damage_types,
                    pl->damage_cycles,
                    pl->combat_cycle,
                    pl->health,
                    pl->total_health);
        }
        /* NPCs */
        for( int i = 0; i < game->npc_count; i++ )
        {
            int nid = game->active_npcs[i];
            struct NPCEntity* npc = &game->npcs[nid];
            if( npc->alive )
            {
                draw_entity_overlays(
                    npc->scene_element,
                    npc->damage_values,
                    npc->damage_types,
                    npc->damage_cycles,
                    npc->combat_cycle,
                    npc->health,
                    npc->total_health);
            }
        }
    }

    int camera_tile_x = game->camera_world_x / 128;
    int camera_tile_z = game->camera_world_z / 128;

    int radius = 25;
    int sw_x = camera_tile_x - radius;
    int sw_z = camera_tile_z - radius;
    int ne_x = camera_tile_x + radius;
    int ne_z = camera_tile_z + radius;

    struct MinimapRenderCommandBuffer* minimap_command_buffer = minimap_commands_new(1024);
    if( game->sys_minimap )
        minimap_render(game->sys_minimap, sw_x, sw_z, ne_x, ne_z, 0, minimap_command_buffer);

    int rgb_foreground;
    int rgb_background;
    int shape;
    int angle;
    for( int i = 0; i < minimap_command_buffer->count; i++ )
    {
        struct MinimapRenderCommand* command = &minimap_command_buffer->commands[i];
        switch( command->kind )
        {
        case MINIMAP_RENDER_COMMAND_LOC:
        {
            break;
        }
        case MINIMAP_RENDER_COMMAND_TILE:
        {
            shape = minimap_tile_shape(
                game->sys_minimap, command->_tile.tile_sx, command->_tile.tile_sz, 0);

            angle = minimap_tile_rotation(
                game->sys_minimap, command->_tile.tile_sx, command->_tile.tile_sz, 0);
            rgb_background = minimap_tile_rgb(
                game->sys_minimap,
                command->_tile.tile_sx,
                command->_tile.tile_sz,
                0,
                MINIMAP_BACKGROUND);
            rgb_foreground = minimap_tile_rgb(
                game->sys_minimap,
                command->_tile.tile_sx,
                command->_tile.tile_sz,
                0,
                MINIMAP_FOREGROUND);
            if( rgb_foreground == 0 && rgb_background == 0 )
                break;

            // Write minimap starting at (0,0) in the buffer
            // X: tile_sx increases from sw_x to ne_x, map to 0 to (ne_x - sw_x) * 4
            int tile_x = (command->_tile.tile_sx - sw_x) * 4;
            // Y: tile_sz increases from sw_z to ne_z, but we want ne_z at top (y=0) and sw_z at
            // bottom Original: minimap_center_y - (tile_sz + 1 - sw_z) * 4 For (0,0) start: (ne_z -
            // tile_sz) * 4
            int tile_y = (ne_z - command->_tile.tile_sz) * 4;
            dash2d_fill_minimap_tile(
                renderer->minimap_buffer,
                renderer->minimap_buffer_width,
                tile_x,
                tile_y,
                rgb_background,
                rgb_foreground,
                angle,
                shape,
                renderer->minimap_buffer_width,
                renderer->minimap_buffer_height);

            {
                int wall = minimap_tile_wall(
                    game->sys_minimap, command->_tile.tile_sx, command->_tile.tile_sz, 0);
                if( wall != 0 )
                {
                    int tx = tile_x;
                    int ty = tile_y;
                    dash2d_draw_minimap_wall(
                        renderer->minimap_buffer,
                        renderer->minimap_buffer_width,
                        tx,
                        ty,
                        wall,
                        renderer->minimap_buffer_width,
                        renderer->minimap_buffer_height);
                }
            }
        }

        break;
        }
    }
    minimap_commands_free(minimap_command_buffer);

    int mm_side = 800 - (512 + 10);
    blit_rotated_buffer(
        renderer->minimap_buffer,
        renderer->minimap_buffer_width,
        renderer->minimap_buffer_height,
        renderer->minimap_buffer_width >> 1,
        renderer->minimap_buffer_height >> 1,
        renderer->pixel_buffer,
        renderer->width,
        550 + 25,
        4 + 5,
        146,
        151,
        73,
        75,
        game->camera_yaw);

    if( game->sprite_compass )
        blit_rotated_buffer(
            (int*)game->sprite_compass->pixels_argb,
            game->sprite_compass->width,
            game->sprite_compass->height,
            game->sprite_compass->width >> 1,
            game->sprite_compass->height >> 1,
            renderer->pixel_buffer,
            renderer->width,
            550,
            4,
            33,
            33,
            // The game always assumes the dest anchor is the center of the the width and height.
            16,
            16,
            game->camera_yaw);

    if( game->sprite_invback )
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_invback,
            game->iface_view_port,
            553,
            205,
            renderer->pixel_buffer);

    /* Chat Y: below viewport. Client.ts viewport 334px -> bottom 338; bar at 338, chat at 357.
     * C viewport may differ; align chat to viewport_bottom + 19. */
    int chat_y = 357;
    if( game->view_port )
        chat_y = game->view_port->height + 19;

    if( game->sprite_backleft1 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backleft1,
            game->iface_view_port,
            0,
            4,
            renderer->pixel_buffer);
    }

    if( game->sprite_backleft2 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backleft2,
            game->iface_view_port,
            0,
            chat_y,
            renderer->pixel_buffer);
    }

    if( game->sprite_backright1 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backright1,
            game->iface_view_port,
            722,
            4,
            renderer->pixel_buffer);
    }

    if( game->sprite_backright2 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backright2,
            game->iface_view_port,
            743,
            205,
            renderer->pixel_buffer);
    }

    if( game->sprite_backtop1 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backtop1,
            game->iface_view_port,
            0,
            0,
            renderer->pixel_buffer);
    }

    if( game->sprite_backvmid1 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backvmid1,
            game->iface_view_port,
            516,
            4,
            renderer->pixel_buffer);
    }

    if( game->sprite_backvmid2 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backvmid2,
            game->iface_view_port,
            516,
            205,
            renderer->pixel_buffer);
    }

    if( game->sprite_backvmid3 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backvmid3,
            game->iface_view_port,
            496,
            chat_y,
            renderer->pixel_buffer);
    }

    if( game->sprite_backhmid2 )
    {
        /* Bar between viewport and chat: 19px above chat */
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backhmid2,
            game->iface_view_port,
            0,
            chat_y - 19,
            renderer->pixel_buffer);
    }

    if( game->sprite_mapback )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_mapback,
            game->iface_view_port,
            550,
            4,
            renderer->pixel_buffer);
    }

    /* Top tab strip at (516, 160) - Client.ts areaBackhmid1.draw(516, 160) */
    int bind_x = 516;
    int bind_y = 160;

    if( game->sprite_backhmid1 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backhmid1,
            game->iface_view_port,
            bind_x,
            bind_y,
            renderer->pixel_buffer);
    }

    /* Redstone for selected tab (top row 0-6) - Client.ts lines 4459-4471, only when sidebar is -1
     */
    if( game->sidebar_interface_id == -1 && game->selected_tab >= 0 && game->selected_tab <= 6 )
    {
        struct DashSprite* redstone = NULL;
        int rx = 0, ry = 0;
        switch( game->selected_tab )
        {
        case 0:
            redstone = game->sprite_redstone1;
            rx = 22;
            ry = 10;
            break;
        case 1:
            redstone = game->sprite_redstone2;
            rx = 54;
            ry = 8;
            break;
        case 2:
            redstone = game->sprite_redstone2;
            rx = 82;
            ry = 8;
            break;
        case 3:
            redstone = game->sprite_redstone3;
            rx = 110;
            ry = 8;
            break;
        case 4:
            redstone = game->sprite_redstone2h;
            rx = 153;
            ry = 8;
            break;
        case 5:
            redstone = game->sprite_redstone2h;
            rx = 181;
            ry = 8;
            break;
        case 6:
            redstone = game->sprite_redstone1h;
            rx = 209;
            ry = 9;
            break;
        default:
            break;
        }
        if( redstone )
            dash2d_blit_sprite(
                game->sys_dash,
                redstone,
                game->iface_view_port,
                bind_x + rx,
                bind_y + ry,
                renderer->pixel_buffer);
    }

    if( game->sprite_sideicons[0] )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[0],
            game->iface_view_port,
            bind_x + 29,
            bind_y + 13,
            renderer->pixel_buffer);
    }

    if( game->sprite_sideicons[1] )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[1],
            game->iface_view_port,
            bind_x + 53,
            bind_y + 11,
            renderer->pixel_buffer);
    }

    if( game->sprite_sideicons[2] )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[2],
            game->iface_view_port,
            bind_x + 82,
            bind_y + 11,
            renderer->pixel_buffer);
    }

    if( game->sprite_sideicons[3] )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[3],
            game->iface_view_port,
            bind_x + 115,
            bind_y + 12,
            renderer->pixel_buffer);
    }

    if( game->sprite_sideicons[4] )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[4],
            game->iface_view_port,
            bind_x + 153,
            bind_y + 13,
            renderer->pixel_buffer);
    }

    if( game->sprite_sideicons[5] )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[5],
            game->iface_view_port,
            bind_x + 180,
            bind_y + 11,
            renderer->pixel_buffer);
    }

    if( game->sprite_sideicons[6] )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[6],
            game->iface_view_port,
            bind_x + 208,
            bind_y + 13,
            renderer->pixel_buffer);
    }

    /* Bottom tab strip at (496, 466) - Client.ts areaBackbase2.draw(496, 466) */
    int bind_bottom_x = 496;
    int bind_bottom_y = 466;
    if( game->sprite_backbase2 )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backbase2,
            game->iface_view_port,
            bind_bottom_x,
            bind_bottom_y,
            renderer->pixel_buffer);
    }

    /* Redstone for selected tab (bottom row 7-13) - Client.ts lines 4512-4526 */
    if( game->sidebar_interface_id == -1 && game->selected_tab >= 7 && game->selected_tab <= 13 )
    {
        struct DashSprite* redstone = NULL;
        int rx = 0, ry = 0;
        switch( game->selected_tab )
        {
        case 7:
            redstone = game->sprite_redstone1v;
            rx = 42;
            ry = 0;
            break;
        case 8:
            redstone = game->sprite_redstone2v;
            rx = 74;
            ry = 0;
            break;
        case 9:
            redstone = game->sprite_redstone2v;
            rx = 102;
            ry = 0;
            break;
        case 10:
            redstone = game->sprite_redstone3v;
            rx = 130;
            ry = 1;
            break;
        case 11:
            redstone = game->sprite_redstone2hv;
            rx = 173;
            ry = 0;
            break;
        case 12:
            redstone = game->sprite_redstone2hv;
            rx = 201;
            ry = 0;
            break;
        case 13:
            redstone = game->sprite_redstone1hv;
            rx = 229;
            ry = 0;
            break;
        default:
            break;
        }
        if( redstone )
            dash2d_blit_sprite(
                game->sys_dash,
                redstone,
                game->iface_view_port,
                bind_bottom_x + rx,
                bind_bottom_y + ry,
                renderer->pixel_buffer);
    }

    /* Bottom row sideicons (tabs 7-13) - Client.ts lines 4529-4551 */
    if( game->sprite_sideicons[7] )
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[7],
            game->iface_view_port,
            bind_bottom_x + 74,
            bind_bottom_y + 2,
            renderer->pixel_buffer);
    if( game->sprite_sideicons[8] )
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[8],
            game->iface_view_port,
            bind_bottom_x + 102,
            bind_bottom_y + 3,
            renderer->pixel_buffer);
    if( game->sprite_sideicons[9] )
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[9],
            game->iface_view_port,
            bind_bottom_x + 137,
            bind_bottom_y + 4,
            renderer->pixel_buffer);
    if( game->sprite_sideicons[10] )
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[10],
            game->iface_view_port,
            bind_bottom_x + 174,
            bind_bottom_y + 2,
            renderer->pixel_buffer);
    if( game->sprite_sideicons[11] )
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[11],
            game->iface_view_port,
            bind_bottom_x + 201,
            bind_bottom_y + 2,
            renderer->pixel_buffer);
    if( game->sprite_sideicons[12] )
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_sideicons[12],
            game->iface_view_port,
            bind_bottom_x + 226,
            bind_bottom_y + 2,
            renderer->pixel_buffer);

    // Initialize clipping bounds for interface viewport
    if( game->iface_view_port )
    {
        dash2d_set_bounds(
            game->iface_view_port,
            0,
            0,
            game->iface_view_port->width,
            game->iface_view_port->height);
    }

    // Render interfaces if set
    if( game->viewport_interface_id != -1 )
    {
        struct CacheDatConfigComponent* viewport_component =
            buildcachedat_get_component(game->buildcachedat, game->viewport_interface_id);
        if( viewport_component )
        {
            game->current_hovered_interface_id = interface_find_hovered_interface_id(
                game, viewport_component, 0, 0, game->mouse_x, game->mouse_y);
            interface_draw_component(
                game, viewport_component, 0, 0, 0, renderer->pixel_buffer, renderer->width);
        }
    }

    // Render sidebar interface (inventory area at 553, 205)
    if( game->sidebar_interface_id != -1 )
    {
        printf(
            "DEBUG: Attempting to render sidebar interface ID: %d\n", game->sidebar_interface_id);

        struct CacheDatConfigComponent* sidebar_component =
            buildcachedat_get_component(game->buildcachedat, game->sidebar_interface_id);

        if( sidebar_component )
        {
            game->current_hovered_interface_id = interface_find_hovered_interface_id(
                game, sidebar_component, 553, 205, game->mouse_x, game->mouse_y);
            printf("DEBUG: Sidebar component found\n");
            printf(
                "  Component type: %d, width: %d, height: %d\n",
                sidebar_component->type,
                sidebar_component->width,
                sidebar_component->height);
            if( sidebar_component->type == COMPONENT_TYPE_INV )
            {
                printf(
                    "  INV component - invSlotObjId=%p, invSlotObjCount=%p\n",
                    (void*)sidebar_component->invSlotObjId,
                    (void*)sidebar_component->invSlotObjCount);
                if( sidebar_component->invSlotObjId )
                {
                    int total_slots = sidebar_component->width * sidebar_component->height;
                    int filled_slots = 0;
                    for( int i = 0; i < total_slots; i++ )
                    {
                        if( sidebar_component->invSlotObjId[i] > 0 )
                            filled_slots++;
                    }
                    printf("  Filled slots: %d/%d\n", filled_slots, total_slots);
                }
            }
            interface_draw_component(
                game, sidebar_component, 553, 205, 0, renderer->pixel_buffer, renderer->width);
            printf("DEBUG: Sidebar component drawn\n");
        }
        else
        {
            printf(
                "DEBUG: WARNING - Sidebar component %d not found in buildcachedat!\n",
                game->sidebar_interface_id);
        }
    }
    else if(
        game->selected_tab >= 0 && game->selected_tab < 14 &&
        game->tab_interface_id[game->selected_tab] != -1 )
    {
        // If no sidebar interface override, draw the active tab interface
        int tab_component_id = game->tab_interface_id[game->selected_tab];
        // printf("DEBUG: Rendering tab %d interface ID: %d\n", game->selected_tab,
        // tab_component_id);

        struct CacheDatConfigComponent* tab_component =
            buildcachedat_get_component(game->buildcachedat, tab_component_id);

        if( tab_component )
        {
            game->current_hovered_interface_id = interface_find_hovered_interface_id(
                game, tab_component, 553, 205, game->mouse_x, game->mouse_y);
            // printf("DEBUG: Tab component found\n");
            // printf(
            //     "  Component type: %d, width: %d, height: %d\n",
            //     tab_component->type,
            //     tab_component->width,
            //     tab_component->height);
            interface_draw_component(
                game, tab_component, 553, 205, 0, renderer->pixel_buffer, renderer->width);
            // printf("DEBUG: Tab component drawn\n");
        }
        else
        {
            printf(
                "DEBUG: WARNING - Tab component %d not found in buildcachedat!\n",
                tab_component_id);
        }
    }

    /* Chat area at (17, chat_y) - chat_y computed above from viewport */
    if( game->sprite_chatback )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_chatback,
            game->iface_view_port,
            17,
            chat_y,
            renderer->pixel_buffer);
    }
    if( game->chat_interface_id != -1 )
    {
        struct CacheDatConfigComponent* chat_component =
            buildcachedat_get_component(game->buildcachedat, game->chat_interface_id);
        if( chat_component )
        {
            game->current_hovered_interface_id = interface_find_hovered_interface_id(
                game, chat_component, 17, chat_y, game->mouse_x, game->mouse_y);
            interface_draw_component(
                game, chat_component, 17, chat_y, 0, renderer->pixel_buffer, renderer->width);
        }
    }
    else if( game->pixfont_p12 )
    {
        /* Client.ts drawChat: draw messages and input line when chat interface is closed. */
        int chat_x = 17 + 4;
        int chat_y_base = chat_y;
        int line_height = 14;
        int line = 0;
        int stride = renderer->width;
        int* pix = renderer->pixel_buffer;
        static const int black = 0x000000;
        static const int blue = 0x0000FF;
        static const int dark_red = 0x8B0000;
        struct DashViewPort* vp = game->iface_view_port;
        int cl = vp->clip_left;
        int ct = vp->clip_top;
        int cr = vp->clip_right;
        int cb = vp->clip_bottom;

        for( int i = 0; i < GAME_CHAT_MAX; i++ )
        {
            if( !game->message_text[i][0] )
                continue;
            int y = chat_y_base + game->chat_scroll_offset + 70 - line * line_height;
            if( y < chat_y_base - 20 )
                break;
            int type = game->message_type[i];
            const char* sender = game->message_sender[i];
            const char* text = game->message_text[i];
            bool draw_line = false;
            if( type == 0 )
            {
                if( y > chat_y_base && y < chat_y_base + 110 )
                {
                    dashfont_draw_text_clipped(
                        game->pixfont_p12,
                        (uint8_t*)text,
                        chat_x,
                        y,
                        black,
                        pix,
                        stride,
                        cl,
                        ct,
                        cr,
                        cb);
                }
                draw_line = true;
            }
            else if(
                (type == 1 || type == 2) &&
                (type == 1 || game->chat_public_mode == 0 || game->chat_public_mode == 1) )
            {
                if( y > chat_y_base && y < chat_y_base + 110 )
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "%s: ", sender);
                    dashfont_draw_text_clipped(
                        game->pixfont_p12,
                        (uint8_t*)buf,
                        chat_x,
                        y,
                        black,
                        pix,
                        stride,
                        cl,
                        ct,
                        cr,
                        cb);
                    int sx = chat_x + dashfont_text_width(game->pixfont_p12, (uint8_t*)buf) + 8;
                    dashfont_draw_text_clipped(
                        game->pixfont_p12,
                        (uint8_t*)text,
                        sx,
                        y,
                        blue,
                        pix,
                        stride,
                        cl,
                        ct,
                        cr,
                        cb);
                }
                draw_line = true;
            }
            else if(
                (type == 3 || type == 7) &&
                (type == 7 || game->chat_private_mode == 0 || game->chat_private_mode == 1) )
            {
                if( y > chat_y_base && y < chat_y_base + 110 )
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "From %s: ", sender);
                    dashfont_draw_text_clipped(
                        game->pixfont_p12,
                        (uint8_t*)"From ",
                        chat_x,
                        y,
                        black,
                        pix,
                        stride,
                        cl,
                        ct,
                        cr,
                        cb);
                    int x = chat_x + dashfont_text_width(game->pixfont_p12, (uint8_t*)"From ");
                    snprintf(buf, sizeof(buf), "%s: ", sender);
                    dashfont_draw_text_clipped(
                        game->pixfont_p12, (uint8_t*)buf, x, y, black, pix, stride, cl, ct, cr, cb);
                    x += dashfont_text_width(game->pixfont_p12, (uint8_t*)buf) + 8;
                    dashfont_draw_text_clipped(
                        game->pixfont_p12,
                        (uint8_t*)text,
                        x,
                        y,
                        dark_red,
                        pix,
                        stride,
                        cl,
                        ct,
                        cr,
                        cb);
                }
                draw_line = true;
            }
            else if( type == 5 || type == 6 )
            {
                if( y > chat_y_base && y < chat_y_base + 110 && game->chat_private_mode < 2 )
                {
                    if( type == 6 )
                    {
                        char buf[256];
                        snprintf(buf, sizeof(buf), "To %s: ", sender);
                        dashfont_draw_text_clipped(
                            game->pixfont_p12,
                            (uint8_t*)buf,
                            chat_x,
                            y,
                            black,
                            pix,
                            stride,
                            cl,
                            ct,
                            cr,
                            cb);
                        int sx =
                            chat_x + dashfont_text_width(game->pixfont_p12, (uint8_t*)buf) + 12;
                        dashfont_draw_text_clipped(
                            game->pixfont_p12,
                            (uint8_t*)text,
                            sx,
                            y,
                            dark_red,
                            pix,
                            stride,
                            cl,
                            ct,
                            cr,
                            cb);
                    }
                    else
                    {
                        dashfont_draw_text_clipped(
                            game->pixfont_p12,
                            (uint8_t*)text,
                            chat_x,
                            y,
                            dark_red,
                            pix,
                            stride,
                            cl,
                            ct,
                            cr,
                            cb);
                    }
                }
                draw_line = true;
            }
            if( draw_line )
                line++;
        }

        game->chat_scroll_height = line * line_height + 7;
        if( game->chat_scroll_height < 78 )
            game->chat_scroll_height = 78;
    }

    /* Client.ts redrawPrivacySettings: backbase1 at (0, 453), 496x50; four buttons:
     * Public chat (center 55), Private chat (184), Trade/duel (324), Report abuse (458).
     * If buffer height < 503, draw panel at bottom so it is visible. */
#define PRIVACY_PANEL_X 0
#define PRIVACY_PANEL_W 496
#define PRIVACY_PANEL_H 50
    int privacy_panel_y =
        (453 + PRIVACY_PANEL_H <= renderer->height) ? 453 : (renderer->height - PRIVACY_PANEL_H);
    if( privacy_panel_y < 0 )
        privacy_panel_y = 0;
    if( game->sprite_backbase1 && privacy_panel_y + PRIVACY_PANEL_H <= renderer->height )
    {
        dash2d_blit_sprite(
            game->sys_dash,
            game->sprite_backbase1,
            game->iface_view_port,
            PRIVACY_PANEL_X,
            privacy_panel_y,
            renderer->pixel_buffer);
        if( game->pixfont_p12 )
        {
            struct DashViewPort* vp = game->iface_view_port;
            int cl = vp->clip_left;
            int ct = vp->clip_top;
            int cr = vp->clip_right;
            int cb = vp->clip_bottom;
            dash2d_set_bounds(
                vp,
                PRIVACY_PANEL_X,
                privacy_panel_y,
                PRIVACY_PANEL_X + PRIVACY_PANEL_W,
                privacy_panel_y + PRIVACY_PANEL_H);
            int py = privacy_panel_y;
            int stride = renderer->width;
            int* pix = renderer->pixel_buffer;
            int fh = game->pixfont_p12->height2d;
            /* Client.ts: drawStringTaggableCenter(centerX, lineY, ...); font does y -= height2d */
            int label_y = py + 28 - fh;
            int value_y = py + 41 - fh;
            int report_y = py + 33 - fh;
            static const int white = 0xFFFFFF;
            static const int green = 0x00FF00;
            static const int yellow = 0xFFFF00;
            static const int red = 0xFF0000;
            static const int cyan = 0x00FFFF;

            auto draw_center = [&](int center_x, int y, const char* text, int colour) {
                int w = dashfont_text_width(game->pixfont_p12, (uint8_t*)text);
                int x = center_x - (w / 2);
                dashfont_draw_text_clipped(
                    game->pixfont_p12,
                    (uint8_t*)text,
                    x,
                    y,
                    colour,
                    pix,
                    stride,
                    vp->clip_left,
                    vp->clip_top,
                    vp->clip_right,
                    vp->clip_bottom);
            };

            draw_center(55, label_y, "Public chat", white);
            if( game->chat_public_mode == 0 )
                draw_center(55, value_y, "On", green);
            else if( game->chat_public_mode == 1 )
                draw_center(55, value_y, "Friends", yellow);
            else if( game->chat_public_mode == 2 )
                draw_center(55, value_y, "Off", red);
            else
                draw_center(55, value_y, "Hide", cyan);

            draw_center(184, label_y, "Private chat", white);
            if( game->chat_private_mode == 0 )
                draw_center(184, value_y, "On", green);
            else if( game->chat_private_mode == 1 )
                draw_center(184, value_y, "Friends", yellow);
            else
                draw_center(184, value_y, "Off", red);

            draw_center(324, label_y, "Trade/duel", white);
            if( game->chat_trade_mode == 0 )
                draw_center(324, value_y, "On", green);
            else if( game->chat_trade_mode == 1 )
                draw_center(324, value_y, "Friends", yellow);
            else
                draw_center(324, value_y, "Off", red);

            draw_center(458, report_y, "Report abuse", white);

            dash2d_set_bounds(vp, cl, ct, cr, cb);
        }
    }

    /* Draw chat input line (Client.ts font.drawString(4, 90) but GameShell insideChatInputArea
     * Y1=434; chatback at 357, so input at 434-357=77. Use 77 to align with clickable input area.)
     */
    if( game->chat_interface_id == -1 && game->pixfont_p12 )
    {
        int chat_y_base = chat_y;
        int chat_x = 17 + 4;
        static const int black = 0x000000;
        static const int blue = 0x0000FF;
        struct DashViewPort* vp = game->iface_view_port;
        int stride = renderer->width;
        int* pix = renderer->pixel_buffer;
        const char* username = "Player";
        char user_prefix[64];
        snprintf(user_prefix, sizeof(user_prefix), "%s: ", username);
        dashfont_draw_text_clipped(
            game->pixfont_p12,
            (uint8_t*)user_prefix,
            chat_x,
            chat_y_base + 77,
            black,
            pix,
            stride,
            vp->clip_left,
            vp->clip_top,
            vp->clip_right,
            vp->clip_bottom);
        int ix = chat_x + dashfont_text_width(game->pixfont_p12, (uint8_t*)user_prefix) + 6;
        dashfont_draw_text_clipped(
            game->pixfont_p12,
            (uint8_t*)game->chat_typed,
            ix,
            chat_y_base + 77,
            blue,
            pix,
            stride,
            vp->clip_left,
            vp->clip_top,
            vp->clip_right,
            vp->clip_bottom);
        if( game->chat_typed[0] )
        {
            dashfont_draw_text_clipped(
                game->pixfont_p12,
                (uint8_t*)"*",
                ix + dashfont_text_width(game->pixfont_p12, (uint8_t*)game->chat_typed),
                chat_y_base + 77,
                blue,
                pix,
                stride,
                vp->clip_left,
                vp->clip_top,
                vp->clip_right,
                vp->clip_bottom);
        }
    }

    // Draw a vertical line at x = 550
    // for( int y = 0; y < renderer->height; y++ )
    // {
    //     if( 550 >= 0 && 550 < renderer->width )
    //     {
    //         renderer->pixel_buffer[y * renderer->width + 550] = 0xFFFFFF; // white color
    //     }
    // }

    /* Draw highlight polygon into dash_buffer (viewport-local); alpha-blends with 3D scene. */
    if( renderer->highlight_poly_valid && renderer->highlight_poly_n >= 3 &&
        renderer->highlight_poly_n <= PLATFORM_SOFT3D_HIGHLIGHT_POLY_MAX && renderer->dash_buffer )
    {
        int px[PLATFORM_SOFT3D_HIGHLIGHT_POLY_MAX];
        int py[PLATFORM_SOFT3D_HIGHLIGHT_POLY_MAX];
        int dw = renderer->dash_buffer_width;
        int dh = renderer->dash_buffer_height;
        for( int i = 0; i < renderer->highlight_poly_n; i++ )
        {
            px[i] = renderer->highlight_poly_x[i];
            py[i] = renderer->highlight_poly_y[i];
        }
        /* Inclusive clip bounds so we never write past the buffer (valid x: 0..dw-1, y: 0..dh-1).
         */
        int clip_l = 0;
        int clip_t = 0;
        int clip_r = dw > 0 ? dw - 1 : 0;
        int clip_b = dh > 0 ? dh - 1 : 0;
        dash2d_fill_polygon_alpha(
            renderer->dash_buffer,
            dw,
            px,
            py,
            renderer->highlight_poly_n,
            0x00FF00,
            80,
            clip_l,
            clip_t,
            clip_r,
            clip_b);
    }

    /* Blit 3D view (dash_buffer) into pixel_buffer at viewport offset so path overlay draws on top.
     */
    if( renderer->dash_buffer && renderer->dash_buffer_width > 0 &&
        renderer->dash_buffer_height > 0 )
    {
        int* dash_buf = renderer->dash_buffer;
        int dash_w = renderer->dash_buffer_width;
        int dash_h = renderer->dash_buffer_height;
        int off_x = renderer->dash_offset_x;
        int off_y = renderer->dash_offset_y;
        int* pix = renderer->pixel_buffer;
        int pix_stride = renderer->width;
        for( int y = 0; y < dash_h; y++ )
        {
            int dst_y = y + off_y;
            if( dst_y >= 0 && dst_y < renderer->height )
                for( int x = 0; x < dash_w; x++ )
                {
                    int dst_x = x + off_x;
                    if( dst_x >= 0 && dst_x < renderer->width )
                        pix[dst_y * pix_stride + dst_x] = dash_buf[y * dash_w + x];
                }
        }
    }

    /* Draw path: line from waypoint to waypoint, then a small marker at each waypoint.
     * path_tile_x/z are scene-local (same as terrain sx,sz). Draw at tile center height. */
    if( game->path_tile_count > 1 && game->sys_dash && game->view_port && game->camera &&
        game->scene && game->scene->terrain )
    {
        int ox = renderer->dash_offset_x;
        int oy = renderer->dash_offset_y;
        int clip_l = 0;
        int clip_t = 0;
        int clip_r = renderer->width;
        int clip_b = renderer->height;
        int prev_sx = 0, prev_sy = 0;
        int prev_ok = 0;
        const int marker_size = 4; /* half-size of waypoint marker (pixels) */
        const int path_level = 0;

        for( int i = 0; i < game->path_tile_count; i++ )
        {
            int tx = game->path_tile_x[i];
            int tz = game->path_tile_z[i];
            /* Scene position: center of tile (tx*128+64, tz*128+64), y = tile center height -
             * camera. */
            int scene_x = tx * 128 + 64 - game->camera_world_x;
            int scene_z = tz * 128 + 64 - game->camera_world_z;
            int height_center = scene_terrain_height_center(game->scene, tx, tz, path_level);
            int scene_y = height_center - game->camera_world_y;

            int screen_x, screen_y;
            if( dash3d_project_point(
                    game->sys_dash,
                    scene_x,
                    scene_y,
                    scene_z,
                    game->view_port,
                    game->camera,
                    &screen_x,
                    &screen_y) )
            {
                int px = screen_x + ox;
                int py = screen_y + oy;
                if( prev_ok )
                {
                    dash2d_draw_line_alpha(
                        renderer->pixel_buffer,
                        renderer->width,
                        prev_sx,
                        prev_sy,
                        px,
                        py,
                        0x00FFFF,
                        220,
                        clip_l,
                        clip_t,
                        clip_r,
                        clip_b);
                }
                /* Draw small filled square at this waypoint. */
                int m_l = px - marker_size;
                int m_t = py - marker_size;
                int m_w = marker_size * 2;
                int m_h = marker_size * 2;
                if( m_l < clip_l )
                {
                    m_w -= (clip_l - m_l);
                    m_l = clip_l;
                }
                if( m_t < clip_t )
                {
                    m_h -= (clip_t - m_t);
                    m_t = clip_t;
                }
                if( m_l + m_w > clip_r )
                    m_w = clip_r - m_l;
                if( m_t + m_h > clip_b )
                    m_h = clip_b - m_t;
                if( m_w > 0 && m_h > 0 )
                    dash2d_fill_rect_alpha(
                        renderer->pixel_buffer, renderer->width, m_l, m_t, m_w, m_h, 0xFF00FF, 200);
                prev_sx = px;
                prev_sy = py;
                prev_ok = 1;
            }
            else
                prev_ok = 0;
        }
    }

    /* Client.ts: yellow cross (0-3) when tile clicked, red cross (4-7) when viewport clicked but
     * not tile. Draw after dash buffer blit so cross appears on top of 3D view. */
    if( game->sprite_cross[0] && game->mouse_cycle != -1 && game->cross_mode != 0 )
    {
        int frame = game->mouse_cycle / 100;
        if( frame > 3 )
            frame = 3;
        int sprite_idx = frame + (game->cross_mode == 2 ? 4 : 0);
        if( sprite_idx < 8 )
        {
            int cx = renderer->dash_offset_x + game->cross_x - 8 - 4;
            int cy = renderer->dash_offset_y + game->cross_y - 8 - 4;
            dash2d_blit_sprite(
                game->sys_dash,
                game->sprite_cross[sprite_idx],
                game->iface_view_port,
                cx,
                cy,
                renderer->pixel_buffer);
        }
    }

    /* Client.ts drawTooltip: draw hovered loc/NPC/player text at (4, 15) in top left. */
    if( game->hovered_scene_element && game->pixfont_b12 )
    {
        struct SceneElement* el = game->hovered_scene_element;
        char tooltip_buf[128];
        const char* action = "Examine";
        const char* name = NULL;

        if( el->entity_kind == 1 )
        {
            /* NPC: Client.ts addNpcOptions - first op (4..0) or Examine, npc.name */
            struct NPCEntity* npc = (struct NPCEntity*)el->entity_ptr;
            int npc_type_id = el->entity_npc_type_id >= 0 ? el->entity_npc_type_id
                                                          : (npc ? npc->npc_type_id : -1);
            if( npc && game->buildcachedat && npc_type_id >= 0 )
            {
                struct CacheDatConfigNpc* npc_cfg =
                    buildcachedat_get_npc(game->buildcachedat, npc_type_id);
                if( npc_cfg && npc_cfg->name )
                {
                    name = npc_cfg->name;
                    for( int i = 0; i <= 4; i++ )
                    {
                        if( npc_cfg->op[i] && npc_cfg->op[i][0] )
                        {
                            action = npc_cfg->op[i];
                            break;
                        }
                    }
                    if( !action || !action[0] )
                        action = "Examine";
                }
            }
            else if( npc )
            {
                name = "NPC";
            }
        }
        else if( el->entity_kind == 2 )
        {
            /* Player: Client.ts addPlayerOptions - Follow, Trade, Attack, etc. */
            struct PlayerEntity* player = (struct PlayerEntity*)el->entity_ptr;
            if( player && player != &game->players[ACTIVE_PLAYER_SLOT] )
            {
                (void)player;
                action = "Follow";
                name = "Player";
            }
        }
        else if( el->config_loc && el->config_loc->name )
        {
            /* Loc */
            struct CacheConfigLocation* loc = el->config_loc;
            name = loc->name;
            for( int i = 0; i < 10; i++ )
            {
                if( loc->actions[i] && loc->actions[i][0] )
                {
                    action = loc->actions[i];
                    break;
                }
            }
        }

        if( name )
        {
            int len = snprintf(tooltip_buf, sizeof(tooltip_buf), "%s %s", action, name);
            if( len > 0 && len < (int)sizeof(tooltip_buf) )
            {
                int tlx = 4 + renderer->dash_offset_x;
                int tly = 15 + renderer->dash_offset_y;
                int clip_l = 0;
                int clip_t = 0;
                int clip_r = renderer->width;
                int clip_b = renderer->height;
                dashfont_draw_text_clipped(
                    game->pixfont_b12,
                    (uint8_t*)tooltip_buf,
                    tlx + 1,
                    tly + 1,
                    BLACK,
                    renderer->pixel_buffer,
                    renderer->width,
                    clip_l,
                    clip_t,
                    clip_r,
                    clip_b);
                dashfont_draw_text_clipped(
                    game->pixfont_b12,
                    (uint8_t*)tooltip_buf,
                    tlx,
                    tly,
                    WHITE,
                    renderer->pixel_buffer,
                    renderer->width,
                    clip_l,
                    clip_t,
                    clip_r,
                    clip_b);
            }
        }
    }

    /* Draw collision map: red lines at terrain height. Use scene terrain corner heights. */
    if( g_show_collision_map && game->scene && game->scene->collision_maps[0] &&
        game->scene->terrain && game->sys_dash && game->view_port && game->camera )
    {
        struct CollisionMap* cm = game->scene->collision_maps[0];
        struct Scene* scene = game->scene;
        int ox = renderer->dash_offset_x;
        int oy = renderer->dash_offset_y;
        int clip_l = 0;
        int clip_t = 0;
        int clip_r = renderer->width;
        int clip_b = renderer->height;
        const int red = 0xFF0000;
        const int alpha = 200;
        const int coll_level = 0;

        for( int x = 0; x < cm->size_x; x++ )
        {
            for( int z = 0; z < cm->size_z; z++ )
            {
                int idx = x * cm->size_z + z;
                // if( cm->flags[idx] == 0 || cm->flags[idx] == COLL_FLAG_FLOOR )
                // {
                //     continue;
                // }

                /* Tile corners in scene space; each corner uses that tile's height. */
                int sx0 = x * 128 - game->camera_world_x;
                int sz0 = z * 128 - game->camera_world_z;
                int sx1 = (x + 1) * 128 - game->camera_world_x;
                int sz1 = (z + 1) * 128 - game->camera_world_z;

                struct SceneTileHeights tile_heights = { 0 };
                scene_terrain_tile_heights(scene, x, z, coll_level, &tile_heights);
                int h_sw = tile_heights.sw_height;
                int h_se = tile_heights.se_height;
                int h_ne = tile_heights.ne_height;
                int h_nw = tile_heights.nw_height;
                int scene_y_sw = h_sw - game->camera_world_y;
                int scene_y_se = h_se - game->camera_world_y;
                int scene_y_ne = h_ne - game->camera_world_y;
                int scene_y_nw = h_nw - game->camera_world_y;

                int screen_x[4], screen_y[4];
                int ok[4];
                /* sw */
                ok[0] = dash3d_project_point(
                    game->sys_dash,
                    sx0,
                    scene_y_sw,
                    sz0,
                    game->view_port,
                    game->camera,
                    &screen_x[0],
                    &screen_y[0]);
                /* se */
                ok[1] = dash3d_project_point(
                    game->sys_dash,
                    sx1,
                    scene_y_se,
                    sz0,
                    game->view_port,
                    game->camera,
                    &screen_x[1],
                    &screen_y[1]);
                /* ne */
                ok[2] = dash3d_project_point(
                    game->sys_dash,
                    sx1,
                    scene_y_ne,
                    sz1,
                    game->view_port,
                    game->camera,
                    &screen_x[2],
                    &screen_y[2]);
                /* nw */
                ok[3] = dash3d_project_point(
                    game->sys_dash,
                    sx0,
                    scene_y_nw,
                    sz1,
                    game->view_port,
                    game->camera,
                    &screen_x[3],
                    &screen_y[3]);

                bool sw_ok = ok[0];
                bool se_ok = ok[1];
                bool ne_ok = ok[2];
                bool nw_ok = ok[3];

                /* BFS checks the DESTINATION tile when stepping. E.g. step west to (x-1,z): dest
                 * (x-1,z) must not have BLOCK_WEST. So the west edge of (x,z) is blocked when our
                 * west neighbor (x-1,z) has BLOCK_WEST. Likewise: east edge when (x+1,z) has
                 * BLOCK_EAST; south edge when (x,z-1) has BLOCK_SOUTH; north edge when (x,z+1) has
                 * BLOCK_NORTH. */
                bool draw_south_edge = !collision_map_can_step_south(cm, x, z);
                bool draw_north_edge = !collision_map_can_step_north(cm, x, z);
                bool draw_east_edge = !collision_map_can_step_east(cm, x, z);
                bool draw_west_edge = !collision_map_can_step_west(cm, x, z);

                bool has_block_south_west = true;
                bool has_block_south_east = true;
                bool has_block_north_west = true;
                bool has_block_north_east = true;
                if( collision_map_can_step_diagonal_south_west(cm, x, z) )
                {
                    has_block_south_west = false;
                }
                if( collision_map_can_step_diagonal_south_east(cm, x, z) )
                {
                    has_block_south_east = false;
                }
                if( collision_map_can_step_diagonal_north_west(cm, x, z) )
                {
                    has_block_north_west = false;
                }
                if( collision_map_can_step_diagonal_north_east(cm, x, z) )
                {
                    has_block_north_east = false;
                }

                int px[4], py[4];
                for( int i = 0; i < 4; i++ )
                {
                    px[i] = screen_x[i] + ox;
                    py[i] = screen_y[i] + oy;
                }

                int px_sw = px[0];
                int py_sw = py[0];
                int px_se = px[1];
                int py_se = py[1];
                int px_ne = px[2];
                int py_ne = py[2];
                int px_nw = px[3];
                int py_nw = py[3];

                int cx = (px_sw + px_se + px_ne + px_nw) / 4;
                int cy = (py_sw + py_se + py_ne + py_nw) / 4;
                const int dash_len = 6;

                auto draw_edge_dash = [&](int mx, int my) {
                    int dx = cx - mx;
                    int dy = cy - my;
                    double len = std::sqrt(double(dx * dx + dy * dy));
                    if( len < 0.5 )
                        return;
                    int ex = mx + (int)(dx * dash_len / len);
                    int ey = my + (int)(dy * dash_len / len);
                    dash2d_draw_line_alpha(
                        renderer->pixel_buffer,
                        renderer->width,
                        mx,
                        my,
                        ex,
                        ey,
                        red,
                        alpha,
                        clip_l,
                        clip_t,
                        clip_r,
                        clip_b);
                };

                /* Draw four tile edges based on neighbor flags (BFS checks dest tile). */
                if( sw_ok && se_ok && draw_south_edge )
                {
                    dash2d_draw_line_alpha(
                        renderer->pixel_buffer,
                        renderer->width,
                        px_sw,
                        py_sw,
                        px_se,
                        py_se,
                        red,
                        alpha,
                        clip_l,
                        clip_t,
                        clip_r,
                        clip_b);
                    draw_edge_dash((px_sw + px_se) / 2, (py_sw + py_se) / 2);
                }
                if( se_ok && ne_ok && draw_east_edge )
                {
                    dash2d_draw_line_alpha(
                        renderer->pixel_buffer,
                        renderer->width,
                        px_se,
                        py_se,
                        px_ne,
                        py_ne,
                        red,
                        alpha,
                        clip_l,
                        clip_t,
                        clip_r,
                        clip_b);
                    draw_edge_dash((px_se + px_ne) / 2, (py_se + py_ne) / 2);
                }
                if( ne_ok && nw_ok && draw_north_edge )
                {
                    dash2d_draw_line_alpha(
                        renderer->pixel_buffer,
                        renderer->width,
                        px_ne,
                        py_ne,
                        px_nw,
                        py_nw,
                        red,
                        alpha,
                        clip_l,
                        clip_t,
                        clip_r,
                        clip_b);
                    draw_edge_dash((px_nw + px_ne) / 2, (py_nw + py_ne) / 2);
                }
                if( nw_ok && sw_ok && draw_west_edge )
                {
                    dash2d_draw_line_alpha(
                        renderer->pixel_buffer,
                        renderer->width,
                        px_nw,
                        py_nw,
                        px_sw,
                        py_sw,
                        red,
                        alpha,
                        clip_l,
                        clip_t,
                        clip_r,
                        clip_b);
                    draw_edge_dash((px_nw + px_sw) / 2, (py_nw + py_sw) / 2);
                }

                /* Draw diagonal line inward toward center for blocked corners (diagonal blocks). */
                if( sw_ok && has_block_south_west )
                    draw_edge_dash(px_sw, py_sw);
                if( se_ok && has_block_south_east )
                    draw_edge_dash(px_se, py_se);
                if( ne_ok && has_block_north_east )
                    draw_edge_dash(px_ne, py_ne);
                if( nw_ok && has_block_north_west )
                    draw_edge_dash(px_nw, py_nw);
            }
        }
    }

    /* Draw minimenu last so it appears on top of everything. Client.ts drawMenu. */
    if( game->menu_visible && game->menu_area == 0 && game->pixfont_b12 )
    {
        int clip_l = 0;
        int clip_t = 0;
        int clip_r = renderer->width;
        int clip_b = renderer->height;
        minimenu_draw(
            game,
            renderer->pixel_buffer,
            renderer->width,
            clip_l,
            clip_t,
            clip_r,
            clip_b,
            renderer->dash_offset_x,
            renderer->dash_offset_y);
    }

    // Render minimap to buffer starting at (0,0)
    // Calculate the center of the minimap content for rotation anchor

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        renderer->pixel_buffer,
        renderer->width,
        renderer->height,
        32,
        renderer->width * sizeof(int),
        0x00FF0000,
        0x0000FF00,
        0x000000FF,
        0xFF000000);

    // Copy the pixels into the texture
    int* pix_write = NULL;
    int texture_pitch = 0;
    if( SDL_LockTexture(renderer->texture, NULL, (void**)&pix_write, &texture_pitch) < 0 )
        return;

    int row_size = renderer->width * sizeof(int);
    int* src_pixels = (int*)surface->pixels;
    int texture_w = texture_pitch / sizeof(int); // Convert pitch (bytes) to pixels

    uint8_t buffer[100];
    strcpy((char*)buffer, "Hello world!");
    buffer[0] = 163;
    // if( game->pixfont_b12 )
    //     dashfont_draw_text(
    //         game->pixfont_b12,
    //         buffer,
    //         0,
    //         0,
    //         0xFFFFFF,
    //         renderer->dash_buffer,
    //         renderer->dash_buffer_width);

    // if( interacting_scene_element != -1 )
    // {
    //     struct SceneElement* element =
    //         scene_element_at(game->scene->scenery, interacting_scene_element);
    //     if( element )
    //     {
    //         snprintf((char*)buffer, sizeof(buffer), "%s", element->_dbg_name);
    //         if( game->pixfont_b12 )
    //             dashfont_draw_text(
    //                 game->pixfont_b12,
    //                 buffer,
    //                 10,
    //                 10,
    //                 0xFFFF,
    //                 renderer->dash_buffer,
    //                 renderer->dash_buffer_width);
    //     }
    // }

    // Copy dash buffer directly to texture at offset position
    if( renderer->dash_buffer )
    {
        int* dash_buf = renderer->dash_buffer;
        int dash_w = renderer->dash_buffer_width;
        int dash_h = renderer->dash_buffer_height;
        int offset_x = renderer->dash_offset_x;
        int offset_y = renderer->dash_offset_y;

        for( int y = 0; y < dash_h; y++ )
        {
            int dst_y = y + offset_y;
            if( dst_y >= 0 && dst_y < renderer->height )
            {
                for( int x = 0; x < dash_w; x++ )
                {
                    int dst_x = x + offset_x;
                    if( dst_x >= 0 && dst_x < renderer->width )
                    {
                        int src_pixel = dash_buf[y * dash_w + x];
                        pix_write[dst_y * texture_w + dst_x] = src_pixel;
                    }
                }
            }
        }
    }

    for( int src_y = 0; src_y < (renderer->height); src_y++ )
    {
        int* row = &pix_write[(src_y * renderer->width)];
        for( int x = 0; x < renderer->width; x++ )
        {
            int pixel = src_pixels[src_y * renderer->width + x];
            if( pixel != 0 )
            {
                row[x] = pixel;
            }
        }
        // memcpy(row, &src_pixels[(src_y - 0) * renderer->width], row_size);
    }

    // Unlock the texture so that it may be used elsewhere
    SDL_UnlockTexture(renderer->texture);

    // window_width and window_height already retrieved at the top of function
    // Calculate destination rectangle to scale the texture to fill the window
    SDL_Rect dst_rect;
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.w = renderer->width;
    dst_rect.h = renderer->height;

    // Use nearest neighbor (point) filtering when window is larger than rendered size
    // This maintains crisp pixels when scaling up beyond max renderer size
    if( window_width > renderer->width || window_height > renderer->height )
    {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // Nearest neighbor (point sampling)
    }
    else
    {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1"); // Bilinear when scaling down
    }

    // Calculate aspect ratio preserving dimensions
    float src_aspect = (float)renderer->width / (float)renderer->height;
    float window_aspect = (float)window_width / (float)window_height;

    if( src_aspect > window_aspect )
    {
        // Renderer is wider - fit to window width
        dst_rect.w = window_width;
        dst_rect.h = (int)(window_width / src_aspect);
        dst_rect.x = 0;
        dst_rect.y = (window_height - dst_rect.h) / 2;
    }
    else
    {
        // Renderer is taller - fit to window height
        dst_rect.h = window_height;
        dst_rect.w = (int)(window_height * src_aspect);
        dst_rect.y = 0;
        dst_rect.x = (window_width - dst_rect.w) / 2;
    }

    SDL_RenderCopy(renderer->renderer, renderer->texture, NULL, &dst_rect);
    SDL_FreeSurface(surface);

    render_imgui(renderer, game);

    SDL_RenderPresent(renderer->renderer);
}

// static int
// obj_model(
//     struct GGame* game,
//     int obj_id)
// {
//     struct BuildCacheDat* buildcachedat = game->buildcachedat;
//     struct CacheDatConfigObj* obj = buildcachedat_get_obj(buildcachedat, obj_id);
//     if( !obj )
//         return -2;

//     int models_queue[50] = { 0 };
//     int models_queue_count = 0;

//     struct CacheModel* models[50] = { 0 };
//     int build_models_count = 0;

//     int idxs[3] = { 0 };

//     if( obj )
//     {
//         struct CacheModel* model = buildcachedat_get_obj_model(buildcachedat, obj_id);
//         if( model )
//             return obj_id;

//         idxs[0] = obj->manwear;
//         idxs[1] = obj->manwear2;
//         idxs[2] = obj->manwear3;

//         for( int i = 0; i < 3; i++ )
//         {
//             if( idxs[i] >= 0 )
//             {
//                 struct CacheModel* model = buildcachedat_get_model(buildcachedat, idxs[i]);
//                 if( !model )
//                     models_queue[models_queue_count++] = idxs[i];
//                 else
//                     models[build_models_count++] = model;
//             }
//         }

//         if( models_queue_count > 0 )
//         {
//             gametask_new_load_dat(game, models_queue, models_queue_count);
//             return -1;
//         }

//         struct CacheModel* merged_model = model_new_merge(models, build_models_count);
//         buildcachedat_add_obj_model(buildcachedat, obj_id, merged_model);

//         for( int i = 0; i < obj->recol_count; i++ )
//         {
//             model_transform_recolor(merged_model, obj->recol_s[i], obj->recol_d[i]);
//         }
//         return obj_id;
//     }

//     return -1;
// }

// static int
// idk_model(
//     struct GGame* game,
//     int idk_id)
// {
//     struct BuildCacheDat* buildcachedat = game->buildcachedat;
//     struct CacheDatConfigIdk* idk = buildcachedat_get_idk(buildcachedat, idk_id);
//     int models_queue[50] = { 0 };
//     int models_queue_count = 0;

//     if( !idk )
//         return -2;

//     struct CacheModel* models[50] = { 0 };

//     if( idk )
//     {
//         struct CacheModel* model = buildcachedat_get_idk_model(buildcachedat, idk_id);
//         if( model )
//             return idk_id;

//         for( int i = 0; i < idk->models_count; i++ )
//         {
//             int model_id = idk->models[i];
//             struct CacheModel* model = buildcachedat_get_model(buildcachedat, model_id);
//             if( !model )
//                 models_queue[models_queue_count++] = model_id;
//             else
//                 models[i] = model;
//         }

//         if( models_queue_count > 0 )
//         {
//             gametask_new_load_dat(game, models_queue, models_queue_count);
//             return -1;
//         }

//         struct CacheModel* merged_model = NULL;
//         if( idk->models_count > 1 )
//         {
//             merged_model = model_new_merge(models, idk->models_count);
//             for( int i = 0; i < idk->models_count; i++ )
//             {
//                 merged_model->_ids[i] = models[i]->_id;
//             }
//         }
//         else
//         {
//             merged_model = model_new_copy(models[0]);
//             merged_model->_id = idk_id;
//         }

//         buildcachedat_add_idk_model(buildcachedat, idk_id, merged_model);

//         return idk_id;
//     }

//     return -1;
// }

// static int
// obj_model(
//     struct GGame* game,
//     int obj_id)
// {
//     struct BuildCacheDat* buildcachedat = game->buildcachedat;
//     struct CacheDatConfigObj* obj = buildcachedat_get_obj(buildcachedat, obj_id);
//     int models_queue[50] = { 0 };
//     int models_queue_count = 0;

//     struct CacheModel* models[50] = { 0 };

//     if( obj )
//     {
//         struct CacheModel* model = buildcachedat_get_obj_model(buildcachedat, obj_id);
//         if( model )
//             return obj_id;

//         for( int i = 0; i < obj->models_count; i++ )
//         {
//             int model_id = obj->models[i];
//             struct CacheModel* model = buildcachedat_get_model(buildcachedat, model_id);
//             if( !model )
//                 models_queue[models_queue_count++] = model_id;
//             else
//                 models[i] = model;
//         }

//         if( models_queue_count > 0 )
//         {
//             gametask_new_load_dat(game, models_queue, models_queue_count);
//             return -1;
//         }

//         struct CacheModel* merged_model = model_new_merge(models, obj->models_count);
//         buildcachedat_add_obj_model(buildcachedat, obj_id, merged_model);
//         return obj_id;
//     }
//     return -1;
// }

// static void
// build_player(struct GGame* game)
// {
//     struct BuildCacheDat* buildcachedat = game->buildcachedat;

//     int queue_models[50] = { 0 };
//     int queue_models_count = 0;
//     int right_hand_value = -1;
//     int left_hand_value = -1;

//     struct CacheModel* models[50] = { 0 };
//     int build_models[50] = { 0 };
//     int build_models_count = 0;

//     for( int i = 0; i < 12; i++ )
//     {
//         int appearance = game->player_slots[i];
//         if( right_hand_value >= 0 && i == 3 )
//         {
//             appearance = right_hand_value;
//         }
//         if( left_hand_value >= 0 && i == 5 )
//         {
//             appearance = left_hand_value;
//         }
//         if( appearance >= 0x100 && appearance < 0x200 )
//         {
//             appearance -= 0x100;
//             int model_id = idk_model(game, appearance);
//             if( model_id >= 0 )
//             {
//                 struct CacheModel* model = buildcachedat_get_idk_model(buildcachedat, model_id);
//                 assert(model && "Model must be found");
//                 models[build_models_count++] = model;
//             }
//             else if( model_id != -2 )
//             {
//                 game->awaiting_models++;
//             }
//         }
//         else if( appearance >= 0x200 )
//         {
//             appearance -= 0x200;
//             int model_id = obj_model(game, appearance);
//             if( model_id >= 0 )
//             {
//                 struct CacheModel* model = buildcachedat_get_obj_model(buildcachedat, model_id);
//                 assert(model && "Model must be found");
//                 models[build_models_count++] = model;
//             }
//             else if( model_id != -2 )
//             {
//                 game->awaiting_models++;
//             }
//         }
//     }

//     if( game->awaiting_models == 0 && build_models_count > 0 )
//     {
//         struct CacheModel* merged_model = model_new_merge(models, build_models_count);
//         game->model = dashmodel_new_from_cache_model(merged_model);
//         model_free(merged_model);

//         _light_model_default(game->model, 0, 0);

//         game->build_player = 0;
//     }
// }

void
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_ProcessServer(
    struct Platform2_OSX_SDL2_Renderer_Soft3D* renderer,
    struct Server* server,
    struct GGame* game,
    uint64_t timestamp_ms)
{
    if( !server )
    {
        return;
    }

    // if( game->build_player == 1 )
    // {
    //     build_player(game);
    // }

    if( renderer->first_frame == 0 )
    {
        struct ProtConnect connect;
        connect.pid = 0;
        renderer->first_frame = 1;
        renderer->outgoing_message_size = prot_connect_encode(
            &connect, renderer->outgoing_message_buffer, sizeof(renderer->outgoing_message_buffer));
    }

    // Check if a tile was clicked and pack message into buffer
    if( renderer->clicked_tile_x != -1 && renderer->clicked_tile_z != -1 )
    {
        struct ProtTileClick tile_click;
        tile_click.x = renderer->clicked_tile_x;
        tile_click.z = renderer->clicked_tile_z;

        renderer->outgoing_message_size = prot_tile_click_encode(
            &tile_click,
            renderer->outgoing_message_buffer,
            sizeof(renderer->outgoing_message_buffer));

        // Clear the clicked tile after packing the message
        renderer->clicked_tile_x = -1;
        renderer->clicked_tile_z = -1;
        renderer->clicked_tile_level = -1;
    }

    // Send outgoing message to server if there is one
    if( renderer->outgoing_message_size > 0 )
    {
        printf("Sending tile click to server: message size %d\n", renderer->outgoing_message_size);
        server_receive(server, renderer->outgoing_message_buffer, renderer->outgoing_message_size);
        renderer->outgoing_message_size = 0;
    }

    // Step the server (convert timestamp_ms to int, using milliseconds)
    server_step(server, (int)(timestamp_ms % 2147483647)); // Clamp to int range

    // Get and process server's outgoing message (player location update)
    // uint8_t* server_message_data = NULL;
    // int server_message_size = 0;
    // server_get_outgoing_message(server, &server_message_data, &server_message_size);

    // struct RSBuffer buffer;
    // rsbuf_init(&buffer, (int8_t*)server_message_data, server_message_size);

    // while( buffer.position < buffer.size )
    // {
    //     int message_kind = buffer.data[buffer.position];
    //     int message_size = client_prot_get_packet_size((enum ClientProtKind)message_kind);

    //     void* data = buffer.data + buffer.position;
    //     int data_size = message_size - buffer.position;

    //     switch( message_kind )
    //     {
    //     case CLIENT_PROT_PLAYER_MOVE:
    //     {
    //         struct ClientProtPlayerMove player_move;
    //         buffer.position +=
    //             client_prot_player_move_decode(&player_move, (uint8_t*)data, data_size);

    //         game->player_tx = player_move.x;
    //         game->player_tz = player_move.z;
    //     }
    //     break;
    //     case CLIENT_PROT_PLAYER:
    //     {
    //         struct ClientProtPlayer player;
    //         buffer.position += client_prot_player_decode(&player, (uint8_t*)data, data_size);

    //         game->player_tx = player.x;
    //         game->player_tz = player.z;
    //         for( int i = 0; i < 12; i++ )
    //         {
    //             game->player_slots[i] = player.slots[i];
    //         }
    //         game->player_walkanim = player.walkanim;
    //         game->player_runanim = player.runanim;
    //         game->player_walkanim_b = player.walkanim_b;
    //         game->player_walkanim_r = player.walkanim_r;
    //         game->player_walkanim_l = player.walkanim_l;
    //         game->player_turnanim = player.turnanim;
    //         game->player_readyanim = player.readyanim;

    //         game->build_player = 1;
    //     }
    //     break;
    //     }

    //     if( game->player_draw_x == -1 && game->player_draw_z == -1 )
    //     {
    //         game->player_draw_x = game->player_tx * 128;
    //         game->player_draw_z = game->player_tz * 128;
    //     }
    // }
}