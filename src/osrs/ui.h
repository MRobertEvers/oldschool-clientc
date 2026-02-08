#ifndef OSRS_UI_H
#define OSRS_UI_H

#include "graphics/dash.h"

#include <stdbool.h>
#include <stdint.h>

// UI Layout Constants (based on RS2 client dimensions)
#define UI_WIDTH 765
#define UI_HEIGHT 503

#define UI_VIEWPORT_X 4
#define UI_VIEWPORT_Y 4
#define UI_VIEWPORT_WIDTH 512
#define UI_VIEWPORT_HEIGHT 334

#define UI_MINIMAP_X 550
#define UI_MINIMAP_Y 4
#define UI_MINIMAP_WIDTH 213
#define UI_MINIMAP_HEIGHT 203

#define UI_SIDEBAR_X 553
#define UI_SIDEBAR_Y 205
#define UI_SIDEBAR_WIDTH 210
#define UI_SIDEBAR_HEIGHT 293

#define UI_CHATBOX_X 0
#define UI_CHATBOX_Y 338
#define UI_CHATBOX_WIDTH 519
#define UI_CHATBOX_HEIGHT 165

#define UI_CHAT_INPUT_X 7
#define UI_CHAT_INPUT_Y 482
#define UI_CHAT_INPUT_WIDTH 505
#define UI_CHAT_INPUT_HEIGHT 15

// Title screen constants
#define UI_TITLE_WIDTH 765
#define UI_TITLE_HEIGHT 503
#define UI_FLAME_WIDTH 128
#define UI_FLAME_HEIGHT 256

// Color constants (RGB)
#define UI_COLOR_BLACK 0x000000
#define UI_COLOR_WHITE 0xFFFFFF
#define UI_COLOR_RED 0xFF0000
#define UI_COLOR_GREEN 0x00FF00
#define UI_COLOR_BLUE 0x0000FF
#define UI_COLOR_YELLOW 0xFFFF00
#define UI_COLOR_CYAN 0x00FFFF
#define UI_COLOR_MAGENTA 0xFF00FF
#define UI_COLOR_GRAY 0x808080
#define UI_COLOR_DARKGRAY 0x404040
#define UI_COLOR_LIGHTGRAY 0xC0C0C0
#define UI_COLOR_DARKBLUE 0x0000C0
#define UI_COLOR_DARKGREEN 0x00C000
#define UI_COLOR_DARKRED 0xC00000
#define UI_COLOR_ORANGE 0xFF8000
#define UI_COLOR_PURPLE 0x800080

// UI State
enum UIState
{
    UI_STATE_TITLE = 0,
    UI_STATE_LOGIN = 1,
    UI_STATE_GAME = 2,
    UI_STATE_LOADING = 3
};

enum UITitleLoginField
{
    UI_LOGIN_FIELD_USERNAME = 0,
    UI_LOGIN_FIELD_PASSWORD = 1
};

// Flame animation for title screen
struct UIFlameAnimation
{
    int* flame_buffer0;
    int* flame_buffer1;
    int* flame_buffer2;
    int* flame_buffer3;
    int* flame_gradient;
    int* flame_gradient0;
    int* flame_gradient1;
    int* flame_gradient2;
    int flame_line_offset[256];
    int flame_cycle0;
    int flame_gradient_cycle0;
    int flame_gradient_cycle1;
    bool active;
    struct DashPix32* image_flames_left;
    struct DashPix32* image_flames_right;
};

// Chat message
struct UIChatMessage
{
    char* text;
    char* sender;
    int type;
    int modlevel;
};

// UI Context
struct UIContext
{
    enum UIState state;

    // Title/Login screen
    enum UITitleLoginField login_field;
    char username[128];
    char password[128];
    char login_message0[256];
    char login_message1[256];
    struct UIFlameAnimation* flames;

    // Game UI
    bool redraw_frame;
    bool redraw_sidebar;
    bool redraw_chatback;
    bool redraw_minimap;

    // Sidebar
    int selected_tab;
    int tab_interface_id[14];
    int sidebar_interface_id;

    // Chat
    int chat_interface_id;
    int chat_scroll_offset;
    int chat_scroll_height;
    struct UIChatMessage messages[100];
    int message_count;
    char chat_input[256];
    bool chat_input_open;

    // Menu
    bool menu_visible;
    int menu_area;
    int menu_x;
    int menu_y;
    int menu_width;
    int menu_height;
    int menu_size;
    char* menu_options[250];

    // Modal
    bool modal_message_visible;
    char modal_message[256];

    // Mouse
    int mouse_x;
    int mouse_y;
    int mouse_button;

    // Viewport/camera
    struct DashViewPort* viewport;
    struct DashViewPort* iface_viewport;

    // Sprites
    struct DashSprite* sprite_invback;
    struct DashSprite* sprite_chatback;
    struct DashSprite* sprite_mapback;
    struct DashSprite* sprite_backbase1;
    struct DashSprite* sprite_backbase2;
    struct DashSprite* sprite_backhmid1;
    struct DashSprite* sprite_compass;
    struct DashSprite* sprite_mapedge;
    struct DashSprite* sprite_sideicons[13];
    struct DashSprite* sprite_scrollbar0;
    struct DashSprite* sprite_scrollbar1;

    // Fonts
    struct DashPixFont* font_plain11;
    struct DashPixFont* font_plain12;
    struct DashPixFont* font_bold12;
    struct DashPixFont* font_quill8;
};

// ============================================================================
// UI Initialization and Management
// ============================================================================

struct UIContext*
ui_new(void);
void
ui_free(struct UIContext* ui);
void
ui_set_state(
    struct UIContext* ui,
    enum UIState state);

// ============================================================================
// Drawing Functions
// ============================================================================

// Core 2D primitives
void
ui_draw_line(
    int* pixel_buffer,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1,
    int color_rgb);

void
ui_draw_rect_outline(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb);

void
ui_draw_circle(
    int* pixel_buffer,
    int stride,
    int center_x,
    int center_y,
    int radius,
    int color_rgb);

void
ui_draw_horizontal_gradient(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_left,
    int color_right);

void
ui_draw_vertical_gradient(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_top,
    int color_bottom);

// ============================================================================
// Title Screen
// ============================================================================

void
ui_draw_title_screen(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride);

void
ui_draw_login_screen(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride);

// Flame animation
struct UIFlameAnimation*
ui_flame_animation_new(void);
void
ui_flame_animation_free(struct UIFlameAnimation* flames);
void
ui_flame_animation_update(struct UIFlameAnimation* flames);
void
ui_flame_animation_draw(
    struct UIFlameAnimation* flames,
    int* pixel_buffer,
    int stride,
    int x,
    int y);

// ============================================================================
// Game UI
// ============================================================================

void
ui_draw_game_frame(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride);

void
ui_draw_minimap(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride,
    int player_x,
    int player_z,
    int camera_yaw);

void
ui_draw_sidebar(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride);

void
ui_draw_chatbox(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride);

void
ui_draw_menu(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride);

void
ui_draw_tooltip(
    struct UIContext* ui,
    int* pixel_buffer,
    int stride,
    const char* text);

// ============================================================================
// Interface/Component System
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
    bool hovered);

void
ui_draw_scrollbar(
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int height,
    int scroll_position,
    int scroll_height);

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
    bool focused);

// ============================================================================
// Chat
// ============================================================================

void
ui_chat_add_message(
    struct UIContext* ui,
    const char* text,
    const char* sender,
    int type);

void
ui_chat_clear(struct UIContext* ui);

// ============================================================================
// Input Handling
// ============================================================================

void
ui_handle_mouse_click(
    struct UIContext* ui,
    int x,
    int y,
    int button);

void
ui_handle_mouse_move(
    struct UIContext* ui,
    int x,
    int y);

void
ui_handle_key_press(
    struct UIContext* ui,
    int keycode,
    char character);

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
    int rh);

int
ui_lerp_color(
    int color1,
    int color2,
    float t);

#endif // OSRS_UI_H
