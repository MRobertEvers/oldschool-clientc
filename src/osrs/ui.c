#include "ui.h"

#include "colors.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// UI Context Management
// ============================================================================

struct UIContext*
ui_new(void)
{
    struct UIContext* ui = (struct UIContext*)calloc(1, sizeof(struct UIContext));
    if( !ui )
        return NULL;

    ui->state = UI_STATE_TITLE;
    ui->login_field = UI_LOGIN_FIELD_USERNAME;
    ui->redraw_frame = true;
    ui->selected_tab = 3; // Default to inventory tab
    ui->chat_scroll_offset = 0;
    ui->chat_scroll_height = 78;
    ui->message_count = 0;
    ui->menu_visible = false;
    ui->modal_message_visible = false;

    // Initialize tab interface IDs to -1
    for( int i = 0; i < 14; i++ )
    {
        ui->tab_interface_id[i] = -1;
    }
    ui->sidebar_interface_id = -1;
    ui->chat_interface_id = -1;

    // Initialize menu options
    for( int i = 0; i < 250; i++ )
    {
        ui->menu_options[i] = NULL;
    }

    return ui;
}

void
ui_free(struct UIContext* ui)
{
    if( !ui )
        return;

    // Free flame animation
    if( ui->flames )
    {
        ui_flame_animation_free(ui->flames);
    }

    // Free chat messages
    for( int i = 0; i < ui->message_count; i++ )
    {
        if( ui->messages[i].text )
            free(ui->messages[i].text);
        if( ui->messages[i].sender )
            free(ui->messages[i].sender);
    }

    // Free menu options
    for( int i = 0; i < 250; i++ )
    {
        if( ui->menu_options[i] )
            free(ui->menu_options[i]);
    }

    free(ui);
}

void
ui_set_state(
    struct UIContext* ui,
    enum UIState state)
{
    ui->state = state;
    ui->redraw_frame = true;
}

// ============================================================================
// Core 2D Drawing Primitives
// ============================================================================

void
ui_draw_line(
    int* pixel_buffer,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1,
    int color_rgb)
{
    // Bresenham's line algorithm
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while( true )
    {
        if( x0 >= 0 && x0 < stride && y0 >= 0 )
        {
            pixel_buffer[y0 * stride + x0] = color_rgb;
        }

        if( x0 == x1 && y0 == y1 )
            break;

        int e2 = 2 * err;
        if( e2 > -dy )
        {
            err -= dy;
            x0 += sx;
        }
        if( e2 < dx )
        {
            err += dx;
            y0 += sy;
        }
    }
}

void
ui_draw_rect_outline(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb)
{
    // Top
    for( int i = 0; i < width; i++ )
    {
        if( x + i >= 0 && x + i < stride && y >= 0 )
            pixel_buffer[y * stride + x + i] = color_rgb;
    }
    // Bottom
    for( int i = 0; i < width; i++ )
    {
        if( x + i >= 0 && x + i < stride && y + height - 1 >= 0 )
            pixel_buffer[(y + height - 1) * stride + x + i] = color_rgb;
    }
    // Left
    for( int i = 0; i < height; i++ )
    {
        if( x >= 0 && x < stride && y + i >= 0 )
            pixel_buffer[(y + i) * stride + x] = color_rgb;
    }
    // Right
    for( int i = 0; i < height; i++ )
    {
        if( x + width - 1 >= 0 && x + width - 1 < stride && y + i >= 0 )
            pixel_buffer[(y + i) * stride + x + width - 1] = color_rgb;
    }
}

void
ui_draw_circle(
    int* pixel_buffer,
    int stride,
    int center_x,
    int center_y,
    int radius,
    int color_rgb)
{
    // Midpoint circle algorithm
    int x = radius;
    int y = 0;
    int err = 0;

    while( x >= y )
    {
        // Draw 8 octants
        if( center_x + x >= 0 && center_x + x < stride && center_y + y >= 0 )
            pixel_buffer[(center_y + y) * stride + center_x + x] = color_rgb;
        if( center_x + y >= 0 && center_x + y < stride && center_y + x >= 0 )
            pixel_buffer[(center_y + x) * stride + center_x + y] = color_rgb;
        if( center_x - y >= 0 && center_x - y < stride && center_y + x >= 0 )
            pixel_buffer[(center_y + x) * stride + center_x - y] = color_rgb;
        if( center_x - x >= 0 && center_x - x < stride && center_y + y >= 0 )
            pixel_buffer[(center_y + y) * stride + center_x - x] = color_rgb;
        if( center_x - x >= 0 && center_x - x < stride && center_y - y >= 0 )
            pixel_buffer[(center_y - y) * stride + center_x - x] = color_rgb;
        if( center_x - y >= 0 && center_x - y < stride && center_y - x >= 0 )
            pixel_buffer[(center_y - x) * stride + center_x - y] = color_rgb;
        if( center_x + y >= 0 && center_x + y < stride && center_y - x >= 0 )
            pixel_buffer[(center_y - x) * stride + center_x + y] = color_rgb;
        if( center_x + x >= 0 && center_x + x < stride && center_y - y >= 0 )
            pixel_buffer[(center_y - y) * stride + center_x + x] = color_rgb;

        y++;
        err += 1 + 2 * y;
        if( 2 * (err - x) + 1 > 0 )
        {
            x--;
            err += 1 - 2 * x;
        }
    }
}

int
ui_lerp_color(
    int color1,
    int color2,
    float t)
{
    int r1 = (color1 >> 16) & 0xFF;
    int g1 = (color1 >> 8) & 0xFF;
    int b1 = color1 & 0xFF;

    int r2 = (color2 >> 16) & 0xFF;
    int g2 = (color2 >> 8) & 0xFF;
    int b2 = color2 & 0xFF;

    int r = (int)(r1 + (r2 - r1) * t);
    int g = (int)(g1 + (g2 - g1) * t);
    int b = (int)(b1 + (b2 - b1) * t);

    return (r << 16) | (g << 8) | b;
}

void
ui_draw_horizontal_gradient(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_left,
    int color_right)
{
    for( int dy = 0; dy < height; dy++ )
    {
        for( int dx = 0; dx < width; dx++ )
        {
            float t = (float)dx / (float)width;
            int color = ui_lerp_color(color_left, color_right, t);
            int px = x + dx;
            int py = y + dy;
            if( px >= 0 && px < stride && py >= 0 )
            {
                pixel_buffer[py * stride + px] = color;
            }
        }
    }
}

void
ui_draw_vertical_gradient(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_top,
    int color_bottom)
{
    for( int dy = 0; dy < height; dy++ )
    {
        float t = (float)dy / (float)height;
        int color = ui_lerp_color(color_top, color_bottom, t);
        for( int dx = 0; dx < width; dx++ )
        {
            int px = x + dx;
            int py = y + dy;
            if( px >= 0 && px < stride && py >= 0 )
            {
                pixel_buffer[py * stride + px] = color;
            }
        }
    }
}

// ============================================================================
// Title Screen & Flame Animation
// ============================================================================

struct UIFlameAnimation*
ui_flame_animation_new(void)
{
    struct UIFlameAnimation* flames =
        (struct UIFlameAnimation*)calloc(1, sizeof(struct UIFlameAnimation));
    if( !flames )
        return NULL;

    flames->flame_buffer0 = (int*)calloc(128 * 256, sizeof(int));
    flames->flame_buffer1 = (int*)calloc(128 * 256, sizeof(int));
    flames->flame_buffer2 = (int*)calloc(128 * 256, sizeof(int));
    flames->flame_buffer3 = (int*)calloc(128 * 256, sizeof(int));
    flames->flame_gradient = (int*)calloc(256, sizeof(int));
    flames->flame_gradient0 = (int*)calloc(33153, sizeof(int));
    flames->flame_gradient1 = (int*)calloc(33153, sizeof(int));
    flames->flame_gradient2 = (int*)calloc(33153, sizeof(int));

    // Initialize flame gradient (red to yellow to white)
    for( int i = 0; i < 256; i++ )
    {
        float t = (float)i / 256.0f;
        int r = 255;
        int g = (int)(255 * t);
        int b = 0;
        flames->flame_gradient[i] = (r << 16) | (g << 8) | b;
    }

    // Initialize line offsets for wave effect
    for( int i = 0; i < 256; i++ )
    {
        flames->flame_line_offset[i] = (int)(sin(i * 0.0245) * 10.0);
    }

    flames->active = true;
    flames->flame_cycle0 = 0;
    flames->flame_gradient_cycle0 = 0;
    flames->flame_gradient_cycle1 = 0;

    return flames;
}

void
ui_flame_animation_free(struct UIFlameAnimation* flames)
{
    if( !flames )
        return;

    if( flames->flame_buffer0 )
        free(flames->flame_buffer0);
    if( flames->flame_buffer1 )
        free(flames->flame_buffer1);
    if( flames->flame_buffer2 )
        free(flames->flame_buffer2);
    if( flames->flame_buffer3 )
        free(flames->flame_buffer3);
    if( flames->flame_gradient )
        free(flames->flame_gradient);
    if( flames->flame_gradient0 )
        free(flames->flame_gradient0);
    if( flames->flame_gradient1 )
        free(flames->flame_gradient1);
    if( flames->flame_gradient2 )
        free(flames->flame_gradient2);

    free(flames);
}

void
ui_flame_animation_update(struct UIFlameAnimation* flames)
{
    if( !flames || !flames->active )
        return;

    flames->flame_cycle0 = (flames->flame_cycle0 + 1) & 0xFF;
    flames->flame_gradient_cycle0 = (flames->flame_gradient_cycle0 + 2) & 0x7FF;
    flames->flame_gradient_cycle1 = (flames->flame_gradient_cycle1 + 3) & 0x7FF;

    // Simple flame effect: move pixels upward and fade
    for( int x = 0; x < 128; x++ )
    {
        // Add heat at bottom
        flames->flame_buffer0[255 * 128 + x] = (rand() % 256);

        // Move heat upward and fade
        for( int y = 254; y >= 0; y-- )
        {
            int idx = y * 128 + x;
            int below = (y + 1) * 128 + x;

            // Average with neighbors and fade
            int heat = flames->flame_buffer0[below];
            if( x > 0 )
                heat = (heat + flames->flame_buffer0[below - 1]) / 2;
            if( x < 127 )
                heat = (heat + flames->flame_buffer0[below + 1]) / 2;

            // Fade as it rises
            heat = (heat * 250) / 256;
            flames->flame_buffer0[idx] = heat;
        }
    }
}

void
ui_flame_animation_draw(
    struct UIFlameAnimation* flames,
    int* pixel_buffer,
    int stride,
    int x,
    int y)
{
    if( !flames || !flames->active )
        return;

    // Draw flame effect
    for( int dy = 0; dy < 256; dy++ )
    {
        int offset = flames->flame_line_offset[(dy + flames->flame_cycle0) & 0xFF];
        for( int dx = 0; dx < 128; dx++ )
        {
            int heat = flames->flame_buffer0[dy * 128 + dx];
            if( heat > 0 )
            {
                int px = x + dx + offset;
                int py = y + dy;
                if( px >= 0 && px < stride && py >= 0 )
                {
                    int color = flames->flame_gradient[heat];
                    pixel_buffer[py * stride + px] = color;
                }
            }
        }
    }
}

void
ui_draw_title_screen(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride)
{
    // Fill background
    dash2d_fill_rect(pixel_buffer, stride, 0, 0, UI_TITLE_WIDTH, UI_TITLE_HEIGHT, 0x000000);

    // Draw title text
    if( ui->font_bold12 )
    {
        const char* title = "RuneScape";
        dashfont_draw_text(
            ui->font_bold12,
            (uint8_t*)title,
            UI_TITLE_WIDTH / 2 - 50,
            100,
            UI_COLOR_YELLOW,
            pixel_buffer,
            stride);
    }

    // Draw flames on sides
    if( ui->flames )
    {
        ui_flame_animation_draw(ui->flames, pixel_buffer, stride, 100, 200);
        ui_flame_animation_draw(ui->flames, pixel_buffer, stride, 537, 200);
    }

    // Draw login box
    int login_box_x = UI_TITLE_WIDTH / 2 - 150;
    int login_box_y = UI_TITLE_HEIGHT / 2 - 100;
    int login_box_width = 300;
    int login_box_height = 200;

    // Box background
    dash2d_fill_rect(
        pixel_buffer,
        stride,
        login_box_x,
        login_box_y,
        login_box_width,
        login_box_height,
        0x3E3529);

    // Box outline
    ui_draw_rect_outline(
        pixel_buffer,
        stride,
        login_box_x,
        login_box_y,
        login_box_width,
        login_box_height,
        UI_COLOR_BLACK);
}

void
ui_draw_login_screen(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride)
{
    ui_draw_title_screen(ui, pixel_buffer, stride);

    int login_box_x = UI_TITLE_WIDTH / 2 - 150;
    int login_box_y = UI_TITLE_HEIGHT / 2 - 100;

    // Draw login fields
    if( ui->font_plain12 )
    {
        // Username label
        dashfont_draw_text(
            ui->font_plain12,
            (uint8_t*)"Username:",
            login_box_x + 20,
            login_box_y + 40,
            UI_COLOR_WHITE,
            pixel_buffer,
            stride);

        // Username field
        ui_draw_textbox(
            pixel_buffer,
            stride,
            login_box_x + 20,
            login_box_y + 50,
            260,
            20,
            ui->username,
            ui->font_plain12,
            ui->login_field == UI_LOGIN_FIELD_USERNAME);

        // Password label
        dashfont_draw_text(
            ui->font_plain12,
            (uint8_t*)"Password:",
            login_box_x + 20,
            login_box_y + 90,
            UI_COLOR_WHITE,
            pixel_buffer,
            stride);

        // Password field (masked)
        char masked[128];
        int len = strlen(ui->password);
        for( int i = 0; i < len && i < 127; i++ )
            masked[i] = '*';
        masked[len] = '\0';

        ui_draw_textbox(
            pixel_buffer,
            stride,
            login_box_x + 20,
            login_box_y + 100,
            260,
            20,
            masked,
            ui->font_plain12,
            ui->login_field == UI_LOGIN_FIELD_PASSWORD);

        // Login button
        ui_draw_button(
            pixel_buffer,
            stride,
            login_box_x + 90,
            login_box_y + 140,
            120,
            30,
            "Login",
            ui->font_plain12,
            false,
            false);

        // Login messages
        if( ui->login_message0[0] != '\0' )
        {
            dashfont_draw_text(
                ui->font_plain12,
                (uint8_t*)ui->login_message0,
                login_box_x + 150 - strlen(ui->login_message0) * 3,
                login_box_y + 180,
                UI_COLOR_RED,
                pixel_buffer,
                stride);
        }
    }
}

// ============================================================================
// Game UI
// ============================================================================

void
ui_draw_game_frame(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride)
{
    // Fill background
    dash2d_fill_rect(pixel_buffer, stride, 0, 0, UI_WIDTH, UI_HEIGHT, 0x4D4233);

    // Draw viewport area (black for now, 3D scene draws here)
    dash2d_fill_rect(
        pixel_buffer,
        stride,
        UI_VIEWPORT_X,
        UI_VIEWPORT_Y,
        UI_VIEWPORT_WIDTH,
        UI_VIEWPORT_HEIGHT,
        0x000000);

    // Draw minimap background
    if( ui->sprite_mapback )
    {
        dash2d_blit_sprite(
            NULL, ui->sprite_mapback, NULL, UI_MINIMAP_X, UI_MINIMAP_Y, pixel_buffer);
    }
    else
    {
        dash2d_fill_rect(
            pixel_buffer,
            stride,
            UI_MINIMAP_X,
            UI_MINIMAP_Y,
            UI_MINIMAP_WIDTH,
            UI_MINIMAP_HEIGHT,
            0x3E3529);
    }

    // Draw sidebar background
    if( ui->sprite_invback )
    {
        dash2d_blit_sprite(
            NULL, ui->sprite_invback, NULL, UI_SIDEBAR_X, UI_SIDEBAR_Y, pixel_buffer);
    }
    else
    {
        dash2d_fill_rect(
            pixel_buffer,
            stride,
            UI_SIDEBAR_X,
            UI_SIDEBAR_Y,
            UI_SIDEBAR_WIDTH,
            UI_SIDEBAR_HEIGHT,
            0x3E3529);
    }

    // Draw chatbox background
    if( ui->sprite_chatback )
    {
        dash2d_blit_sprite(
            NULL, ui->sprite_chatback, NULL, UI_CHATBOX_X, UI_CHATBOX_Y, pixel_buffer);
    }
    else
    {
        dash2d_fill_rect(
            pixel_buffer,
            stride,
            UI_CHATBOX_X,
            UI_CHATBOX_Y,
            UI_CHATBOX_WIDTH,
            UI_CHATBOX_HEIGHT,
            0x3E3529);
    }
}

void
ui_draw_minimap(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride,
    int player_x,
    int player_z,
    int camera_yaw)
{
    int minimap_center_x = UI_MINIMAP_X + 94;
    int minimap_center_y = UI_MINIMAP_Y + 83;

    // Draw minimap circle clip (simplified - just draw a circle outline)
    ui_draw_circle(pixel_buffer, stride, minimap_center_x, minimap_center_y, 75, UI_COLOR_BLACK);

    // Draw player dot at center
    dash2d_fill_rect(
        pixel_buffer, stride, minimap_center_x - 1, minimap_center_y - 1, 3, 3, 0xFFFFFF);

    // Draw compass
    if( ui->sprite_compass )
    {
        int compass_x = UI_MINIMAP_X + 164;
        int compass_y = UI_MINIMAP_Y + 6;
        // Would need rotation support for full compass
        dash2d_blit_sprite(NULL, ui->sprite_compass, NULL, compass_x, compass_y, pixel_buffer);
    }
}

void
ui_draw_sidebar(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride)
{
    // Sidebar already has background from ui_draw_game_frame

    // Draw tab icons at bottom
    for( int i = 0; i < 13; i++ )
    {
        if( ui->sprite_sideicons[i] )
        {
            int icon_x = UI_SIDEBAR_X + (i % 7) * 30;
            int icon_y = UI_SIDEBAR_Y + UI_SIDEBAR_HEIGHT - 60 + (i / 7) * 30;
            dash2d_blit_sprite(NULL, ui->sprite_sideicons[i], NULL, icon_x, icon_y, pixel_buffer);

            // Highlight selected tab
            if( i == ui->selected_tab )
            {
                ui_draw_rect_outline(pixel_buffer, stride, icon_x, icon_y, 28, 28, UI_COLOR_WHITE);
            }
        }
    }
}

void
ui_draw_chatbox(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride)
{
    // Chatbox background already drawn

    int chat_x = UI_CHATBOX_X + 7;
    int chat_y = UI_CHATBOX_Y + 7;
    int line_height = 14;

    if( !ui->font_plain12 )
        return;

    // Draw chat messages
    int line = 0;
    for( int i = 0; i < ui->message_count && line < 10; i++ )
    {
        struct UIChatMessage* msg = &ui->messages[i];
        int y = chat_y + ui->chat_scroll_offset + 70 - line * line_height;

        if( y > chat_y && y < chat_y + 100 )
        {
            if( msg->type == 0 )
            {
                // System message
                dashfont_draw_text(
                    ui->font_plain12,
                    (uint8_t*)msg->text,
                    chat_x,
                    y,
                    UI_COLOR_BLACK,
                    pixel_buffer,
                    stride);
            }
            else if( msg->type == 1 || msg->type == 2 )
            {
                // Player chat
                char full_msg[512];
                snprintf(full_msg, sizeof(full_msg), "%s: %s", msg->sender, msg->text);
                dashfont_draw_text(
                    ui->font_plain12,
                    (uint8_t*)full_msg,
                    chat_x,
                    y,
                    UI_COLOR_BLUE,
                    pixel_buffer,
                    stride);
            }
            line++;
        }
    }

    // Draw chat input if active
    if( ui->chat_input_open )
    {
        char input_display[300];
        snprintf(input_display, sizeof(input_display), "%s*", ui->chat_input);
        dashfont_draw_text(
            ui->font_plain12,
            (uint8_t*)input_display,
            UI_CHAT_INPUT_X,
            UI_CHAT_INPUT_Y,
            UI_COLOR_WHITE,
            pixel_buffer,
            stride);
    }

    // Draw modal message if visible
    if( ui->modal_message_visible && ui->font_bold12 )
    {
        int modal_y = UI_CHATBOX_Y + 40;
        dashfont_draw_text(
            ui->font_bold12,
            (uint8_t*)ui->modal_message,
            UI_WIDTH / 2 - 100,
            modal_y,
            UI_COLOR_BLACK,
            pixel_buffer,
            stride);
        dashfont_draw_text(
            ui->font_bold12,
            (uint8_t*)"Click to continue",
            UI_WIDTH / 2 - 80,
            modal_y + 20,
            UI_COLOR_DARKBLUE,
            pixel_buffer,
            stride);
    }
}

void
ui_draw_menu(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride)
{
    if( !ui->menu_visible || !ui->font_plain12 )
        return;

    // Draw menu background
    dash2d_fill_rect(
        pixel_buffer, stride, ui->menu_x, ui->menu_y, ui->menu_width, ui->menu_height, 0x5D5447);

    // Draw menu border
    ui_draw_rect_outline(
        pixel_buffer,
        stride,
        ui->menu_x,
        ui->menu_y,
        ui->menu_width,
        ui->menu_height,
        UI_COLOR_BLACK);

    // Draw menu options
    int option_y = ui->menu_y + 15;
    for( int i = 0; i < ui->menu_size; i++ )
    {
        if( ui->menu_options[i] )
        {
            int color = UI_COLOR_WHITE;
            // Check if mouse is hovering over this option
            if( ui->mouse_x >= ui->menu_x && ui->mouse_x < ui->menu_x + ui->menu_width &&
                ui->mouse_y >= option_y - 12 && ui->mouse_y < option_y )
            {
                color = UI_COLOR_YELLOW;
            }

            dashfont_draw_text(
                ui->font_plain12,
                (uint8_t*)ui->menu_options[i],
                ui->menu_x + 5,
                option_y,
                color,
                pixel_buffer,
                stride);
            option_y += 15;
        }
    }
}

void
ui_draw_tooltip(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride,
    const char* text)
{
    if( !text || !ui->font_plain12 )
        return;

    int text_width = strlen(text) * 6; // Approximate
    int tooltip_width = text_width + 10;
    int tooltip_height = 20;
    int tooltip_x = ui->mouse_x + 10;
    int tooltip_y = ui->mouse_y - 25;

    // Keep tooltip on screen
    if( tooltip_x + tooltip_width > UI_WIDTH )
        tooltip_x = UI_WIDTH - tooltip_width;
    if( tooltip_y < 0 )
        tooltip_y = 0;

    // Draw tooltip background
    dash2d_fill_rect(
        pixel_buffer, stride, tooltip_x, tooltip_y, tooltip_width, tooltip_height, 0xFFFFA0);

    // Draw tooltip border
    ui_draw_rect_outline(
        pixel_buffer, stride, tooltip_x, tooltip_y, tooltip_width, tooltip_height, UI_COLOR_BLACK);

    // Draw tooltip text
    dashfont_draw_text(
        ui->font_plain12,
        (uint8_t*)text,
        tooltip_x + 5,
        tooltip_y + 14,
        UI_COLOR_BLACK,
        pixel_buffer,
        stride);
}

// ============================================================================
// UI Components
// ============================================================================

void
ui_draw_button(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    const char* text,
    struct DashPixFont* font,
    bool pressed,
    bool hovered)
{
    // Button background
    int bg_color = pressed ? 0x4D4233 : (hovered ? 0x6D6253 : 0x5D5447);
    dash2d_fill_rect(pixel_buffer, stride, x, y, width, height, bg_color);

    // Button border
    int border_color = hovered ? UI_COLOR_WHITE : UI_COLOR_BLACK;
    ui_draw_rect_outline(pixel_buffer, stride, x, y, width, height, border_color);

    // Button text (centered)
    if( text && font )
    {
        int text_width = strlen(text) * 6; // Approximate
        int text_x = x + (width - text_width) / 2;
        int text_y = y + (height + 12) / 2;
        dashfont_draw_text(
            font, (uint8_t*)text, text_x, text_y, UI_COLOR_WHITE, pixel_buffer, stride);
    }
}

void
ui_draw_scrollbar(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int height,
    int scroll_position,
    int scroll_height)
{
    // Scrollbar background
    dash2d_fill_rect(pixel_buffer, stride, x, y, 16, height, 0x4D4233);

    // Scrollbar thumb
    if( scroll_height > 0 )
    {
        int thumb_height = (height * height) / scroll_height;
        if( thumb_height < 20 )
            thumb_height = 20;
        if( thumb_height > height )
            thumb_height = height;

        int thumb_y = y + (scroll_position * (height - thumb_height)) / (scroll_height - height);
        dash2d_fill_rect(pixel_buffer, stride, x + 2, thumb_y, 12, thumb_height, 0x6D6253);
    }
}

void
ui_draw_textbox(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    const char* text,
    struct DashPixFont* font,
    bool focused)
{
    // Textbox background
    dash2d_fill_rect(pixel_buffer, stride, x, y, width, height, 0xFFFFFF);

    // Textbox border
    int border_color = focused ? UI_COLOR_BLUE : UI_COLOR_BLACK;
    ui_draw_rect_outline(pixel_buffer, stride, x, y, width, height, border_color);

    // Textbox text
    if( text && font )
    {
        dashfont_draw_text(
            font, (uint8_t*)text, x + 5, y + 14, UI_COLOR_BLACK, pixel_buffer, stride);
    }

    // Draw cursor if focused
    if( focused )
    {
        int cursor_x = x + 5 + strlen(text) * 6;
        ui_draw_line(
            pixel_buffer, stride, cursor_x, y + 3, cursor_x, y + height - 3, UI_COLOR_BLACK);
    }
}

// ============================================================================
// Chat Functions
// ============================================================================

void
ui_chat_add_message(
    struct UIContext* ui,
    const char* text,
    const char* sender,
    int type)
{
    if( ui->message_count >= 100 )
    {
        // Remove oldest message
        if( ui->messages[0].text )
            free(ui->messages[0].text);
        if( ui->messages[0].sender )
            free(ui->messages[0].sender);

        // Shift messages
        for( int i = 0; i < 99; i++ )
        {
            ui->messages[i] = ui->messages[i + 1];
        }
        ui->message_count--;
    }

    struct UIChatMessage* msg = &ui->messages[ui->message_count];
    msg->text = text ? strdup(text) : NULL;
    msg->sender = sender ? strdup(sender) : NULL;
    msg->type = type;
    msg->modlevel = 0;
    ui->message_count++;

    ui->redraw_chatback = true;
}

void
ui_chat_clear(struct UIContext* ui)
{
    for( int i = 0; i < ui->message_count; i++ )
    {
        if( ui->messages[i].text )
            free(ui->messages[i].text);
        if( ui->messages[i].sender )
            free(ui->messages[i].sender);
    }
    ui->message_count = 0;
    ui->redraw_chatback = true;
}

// ============================================================================
// Input Handling
// ============================================================================

void
ui_handle_mouse_click(
    struct UIContext* ui,
    int x,
    int y,
    int button)
{
    ui->mouse_x = x;
    ui->mouse_y = y;
    ui->mouse_button = button;

    if( ui->state == UI_STATE_LOGIN )
    {
        // Check if clicked on username field
        int login_box_x = UI_TITLE_WIDTH / 2 - 150;
        int login_box_y = UI_TITLE_HEIGHT / 2 - 100;

        if( ui_point_in_rect(x, y, login_box_x + 20, login_box_y + 50, 260, 20) )
        {
            ui->login_field = UI_LOGIN_FIELD_USERNAME;
        }
        else if( ui_point_in_rect(x, y, login_box_x + 20, login_box_y + 100, 260, 20) )
        {
            ui->login_field = UI_LOGIN_FIELD_PASSWORD;
        }
        else if( ui_point_in_rect(x, y, login_box_x + 90, login_box_y + 140, 120, 30) )
        {
            // Login button clicked
            // This would trigger the login process
            snprintf(ui->login_message0, sizeof(ui->login_message0), "Connecting to server...");
        }
    }
    else if( ui->state == UI_STATE_GAME )
    {
        // Check if clicked on sidebar tabs
        if( ui_point_in_rect(x, y, UI_SIDEBAR_X, UI_SIDEBAR_Y + UI_SIDEBAR_HEIGHT - 60, 210, 60) )
        {
            int tab_x = (x - UI_SIDEBAR_X) / 30;
            int tab_y = (y - (UI_SIDEBAR_Y + UI_SIDEBAR_HEIGHT - 60)) / 30;
            int tab = tab_y * 7 + tab_x;
            if( tab >= 0 && tab < 13 )
            {
                ui->selected_tab = tab;
                ui->redraw_sidebar = true;
            }
        }

        // Check menu clicks
        if( ui->menu_visible )
        {
            if( ui_point_in_rect(x, y, ui->menu_x, ui->menu_y, ui->menu_width, ui->menu_height) )
            {
                // Calculate which option was clicked
                int option = (y - ui->menu_y) / 15;
                if( option >= 0 && option < ui->menu_size )
                {
                    // Option clicked - would trigger action here
                }
            }
            ui->menu_visible = false;
        }

        // Dismiss modal message
        if( ui->modal_message_visible )
        {
            ui->modal_message_visible = false;
            ui->redraw_chatback = true;
        }
    }
}

void
ui_handle_mouse_move(
    struct UIContext* ui,
    int x,
    int y)
{
    ui->mouse_x = x;
    ui->mouse_y = y;
}

void
ui_handle_key_press(
    struct UIContext* ui,
    int keycode,
    char character)
{
    if( ui->state == UI_STATE_LOGIN )
    {
        if( ui->login_field == UI_LOGIN_FIELD_USERNAME )
        {
            if( character >= 32 && character < 127 )
            {
                int len = strlen(ui->username);
                if( len < 127 )
                {
                    ui->username[len] = character;
                    ui->username[len + 1] = '\0';
                }
            }
            else if( keycode == 8 || keycode == 127 )
            { // Backspace
                int len = strlen(ui->username);
                if( len > 0 )
                    ui->username[len - 1] = '\0';
            }
        }
        else if( ui->login_field == UI_LOGIN_FIELD_PASSWORD )
        {
            if( character >= 32 && character < 127 )
            {
                int len = strlen(ui->password);
                if( len < 127 )
                {
                    ui->password[len] = character;
                    ui->password[len + 1] = '\0';
                }
            }
            else if( keycode == 8 || keycode == 127 )
            { // Backspace
                int len = strlen(ui->password);
                if( len > 0 )
                    ui->password[len - 1] = '\0';
            }
        }

        // Tab to switch fields
        if( keycode == 9 )
        {
            ui->login_field = (ui->login_field == UI_LOGIN_FIELD_USERNAME)
                                  ? UI_LOGIN_FIELD_PASSWORD
                                  : UI_LOGIN_FIELD_USERNAME;
        }
    }
    else if( ui->state == UI_STATE_GAME && ui->chat_input_open )
    {
        if( character >= 32 && character < 127 )
        {
            int len = strlen(ui->chat_input);
            if( len < 255 )
            {
                ui->chat_input[len] = character;
                ui->chat_input[len + 1] = '\0';
            }
        }
        else if( keycode == 8 || keycode == 127 )
        { // Backspace
            int len = strlen(ui->chat_input);
            if( len > 0 )
                ui->chat_input[len - 1] = '\0';
        }
        else if( keycode == 13 )
        { // Enter
            // Send chat message
            if( ui->chat_input[0] != '\0' )
            {
                ui_chat_add_message(ui, ui->chat_input, "You", 1);
                ui->chat_input[0] = '\0';
            }
            ui->chat_input_open = false;
        }
        ui->redraw_chatback = true;
    }
}

// ============================================================================
// Utilities
// ============================================================================

bool
ui_point_in_rect(
    int px,
    int py,
    int rx,
    int ry,
    int rw,
    int rh)
{
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}
