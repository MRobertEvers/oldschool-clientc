/**
 * UI Integration Example
 *
 * This file demonstrates how to integrate the UI system into the 3D raster game.
 * Add this code to your main game loop or platform renderer.
 */

#include "graphics/dash.h"
#include "osrs/game.h"
#include "osrs/ui.h"

// Example: Global UI context (or add to your game state structure)
static struct UIContext* g_ui = NULL;

/**
 * Initialize the UI system
 * Call this once during game initialization
 */
void
example_ui_init(struct GGame* game)
{
    // Create UI context
    g_ui = ui_new();
    if( !g_ui )
    {
        fprintf(stderr, "Failed to create UI context\n");
        return;
    }

    // Set initial state
    ui_set_state(g_ui, UI_STATE_TITLE);

    // Initialize flame animation for title screen
    g_ui->flames = ui_flame_animation_new();

    // Set up fonts (from game)
    g_ui->font_plain11 = game->pixfont_p11;
    g_ui->font_plain12 = game->pixfont_p12;
    g_ui->font_bold12 = game->pixfont_b12;
    g_ui->font_quill8 = game->pixfont_q8;

    // Set up sprites (from game)
    g_ui->sprite_invback = game->sprite_invback;
    g_ui->sprite_chatback = game->sprite_chatback;
    g_ui->sprite_mapback = game->sprite_mapback;
    g_ui->sprite_backbase1 = game->sprite_backbase1;
    g_ui->sprite_backbase2 = game->sprite_backbase2;
    g_ui->sprite_compass = game->sprite_compass;
    g_ui->sprite_mapedge = game->sprite_mapedge;

    for( int i = 0; i < 13; i++ )
    {
        g_ui->sprite_sideicons[i] = game->sprite_sideicons[i];
    }

    g_ui->sprite_scrollbar0 = game->sprite_scrollbar0;
    g_ui->sprite_scrollbar1 = game->sprite_scrollbar1;

    // Set up viewports
    g_ui->viewport = game->view_port;
    g_ui->iface_viewport = game->iface_view_port;

    // Add a welcome message
    ui_chat_add_message(g_ui, "Welcome to RuneScape!", NULL, 0);
}

/**
 * Update the UI (called every frame)
 */
void
example_ui_update(struct GGame* game)
{
    if( !g_ui )
        return;

    // Update flame animation if on title screen
    if( g_ui->state == UI_STATE_TITLE && g_ui->flames )
    {
        ui_flame_animation_update(g_ui->flames);
    }

    // Update mouse position from game
    g_ui->mouse_x = game->mouse_x;
    g_ui->mouse_y = game->mouse_y;
}

/**
 * Draw the UI (called every frame)
 *
 * @param pixel_buffer The screen buffer to draw to
 * @param stride The width of the screen buffer (typically 765)
 * @param game The game state
 */
void
example_ui_draw(
    int* pixel_buffer,
    int stride,
    struct GGame* game)
{
    if( !g_ui )
        return;

    switch( g_ui->state )
    {
    case UI_STATE_TITLE:
        ui_draw_title_screen(g_ui, pixel_buffer, stride);
        break;

    case UI_STATE_LOGIN:
        ui_draw_login_screen(g_ui, pixel_buffer, stride);
        break;

    case UI_STATE_GAME:
        // Draw game frame (viewport, minimap, sidebar, chatbox backgrounds)
        if( g_ui->redraw_frame )
        {
            ui_draw_game_frame(g_ui, pixel_buffer, stride);
            g_ui->redraw_frame = false;
        }

        // Draw 3D scene in viewport area
        // (The existing 3D rendering code would draw here)

        // Draw minimap
        if( g_ui->redraw_minimap )
        {
            ui_draw_minimap(
                g_ui,
                pixel_buffer,
                stride,
                game->camera_world_x,
                game->camera_world_z,
                game->camera_yaw);
            g_ui->redraw_minimap = false;
        }

        // Draw sidebar
        if( g_ui->redraw_sidebar )
        {
            ui_draw_sidebar(g_ui, pixel_buffer, stride);
            g_ui->redraw_sidebar = false;
        }

        // Draw chatbox
        if( g_ui->redraw_chatback )
        {
            ui_draw_chatbox(g_ui, pixel_buffer, stride);
            g_ui->redraw_chatback = false;
        }

        // Draw menu if visible
        if( g_ui->menu_visible )
        {
            ui_draw_menu(g_ui, pixel_buffer, stride);
        }

        // Draw tooltip if hovering over something
        // (You would determine the tooltip text based on what's hovered)
        // ui_draw_tooltip(g_ui, pixel_buffer, stride, "Example Tooltip");

        break;

    case UI_STATE_LOADING:
        // Draw loading screen
        dash2d_fill_rect(pixel_buffer, stride, 0, 0, UI_WIDTH, UI_HEIGHT, 0x000000);
        if( g_ui->font_bold12 )
        {
            dashfont_draw_text(
                g_ui->font_bold12,
                (uint8_t*)"Loading...",
                UI_WIDTH / 2 - 40,
                UI_HEIGHT / 2,
                UI_COLOR_WHITE,
                pixel_buffer,
                stride);
        }
        break;
    }
}

/**
 * Handle mouse click events
 */
void
example_ui_handle_mouse_click(
    int x,
    int y,
    int button)
{
    if( !g_ui )
        return;

    ui_handle_mouse_click(g_ui, x, y, button);
}

/**
 * Handle mouse move events
 */
void
example_ui_handle_mouse_move(
    int x,
    int y)
{
    if( !g_ui )
        return;

    ui_handle_mouse_move(g_ui, x, y);
}

/**
 * Handle keyboard input
 */
void
example_ui_handle_key_press(
    int keycode,
    char character)
{
    if( !g_ui )
        return;

    ui_handle_key_press(g_ui, keycode, character);
}

/**
 * Cleanup the UI system
 * Call this during game shutdown
 */
void
example_ui_cleanup(void)
{
    if( g_ui )
    {
        ui_free(g_ui);
        g_ui = NULL;
    }
}

/**
 * Example: Switching between UI states
 */
void
example_ui_goto_game(void)
{
    if( g_ui )
    {
        ui_set_state(g_ui, UI_STATE_GAME);
        ui_chat_add_message(g_ui, "Welcome to the game!", NULL, 0);
    }
}

void
example_ui_goto_login(void)
{
    if( g_ui )
    {
        ui_set_state(g_ui, UI_STATE_LOGIN);
    }
}

/**
 * Example: Adding chat messages
 */
void
example_ui_add_system_message(const char* message)
{
    if( g_ui )
    {
        ui_chat_add_message(g_ui, message, NULL, 0);
    }
}

void
example_ui_add_player_message(
    const char* player,
    const char* message)
{
    if( g_ui )
    {
        ui_chat_add_message(g_ui, message, player, 1);
    }
}

/**
 * Example: Opening a menu
 */
void
example_ui_show_menu(
    int x,
    int y,
    const char** options,
    int option_count)
{
    if( !g_ui || option_count > 250 )
        return;

    g_ui->menu_visible = true;
    g_ui->menu_x = x;
    g_ui->menu_y = y;
    g_ui->menu_size = option_count;

    // Calculate menu dimensions
    g_ui->menu_width = 150;
    g_ui->menu_height = option_count * 15 + 5;

    // Set menu options
    for( int i = 0; i < option_count; i++ )
    {
        if( g_ui->menu_options[i] )
            free(g_ui->menu_options[i]);
        g_ui->menu_options[i] = strdup(options[i]);
    }
}

/**
 * Example: Showing a modal message
 */
void
example_ui_show_modal(const char* message)
{
    if( !g_ui )
        return;

    g_ui->modal_message_visible = true;
    strncpy(g_ui->modal_message, message, sizeof(g_ui->modal_message) - 1);
    g_ui->modal_message[sizeof(g_ui->modal_message) - 1] = '\0';
    g_ui->redraw_chatback = true;
}

/**
 * ============================================================================
 * Integration Instructions:
 * ============================================================================
 *
 * 1. In your main game initialization (e.g., platform_impl2_osx_sdl2.cpp):
 *    - Call example_ui_init(game) after loading sprites and fonts
 *
 * 2. In your main game loop:
 *    - Call example_ui_update(game) before rendering
 *    - Call example_ui_draw(pixel_buffer, stride, game) after 3D rendering
 *
 * 3. In your input handling:
 *    - Call example_ui_handle_mouse_click(x, y, button) for mouse clicks
 *    - Call example_ui_handle_mouse_move(x, y) for mouse movement
 *    - Call example_ui_handle_key_press(keycode, char) for keyboard input
 *
 * 4. In your game shutdown:
 *    - Call example_ui_cleanup()
 *
 * 5. To switch UI states:
 *    - Call example_ui_goto_game() when entering the game
 *    - Call example_ui_goto_login() when going back to login
 *
 * 6. To add chat messages:
 *    - Call example_ui_add_system_message("message") for system messages
 *    - Call example_ui_add_player_message("player", "message") for chat
 *
 * 7. To show menus:
 *    - const char* options[] = {"Option 1", "Option 2", "Option 3"};
 *    - example_ui_show_menu(x, y, options, 3);
 *
 * 8. To show modal dialogs:
 *    - example_ui_show_modal("Are you sure?");
 *
 * ============================================================================
 * Platform-Specific Integration Example:
 * ============================================================================
 *
 * In platform_impl2_osx_sdl2_renderer_soft3d.cpp, add to the render function:
 *
 *     // After drawing the 3D scene:
 *     example_ui_draw(
 *         renderer->pixel_buffer,
 *         UI_WIDTH,
 *         game
 *     );
 *
 * In the SDL event handling:
 *
 *     case SDL_MOUSEBUTTONDOWN:
 *         example_ui_handle_mouse_click(
 *             event.button.x,
 *             event.button.y,
 *             event.button.button
 *         );
 *         break;
 *
 *     case SDL_MOUSEMOTION:
 *         example_ui_handle_mouse_move(
 *             event.motion.x,
 *             event.motion.y
 *         );
 *         break;
 *
 *     case SDL_KEYDOWN:
 *         example_ui_handle_key_press(
 *             event.key.keysym.sym,
 *             event.key.keysym.sym < 128 ? (char)event.key.keysym.sym : 0
 *         );
 *         break;
 *
 * ============================================================================
 */
