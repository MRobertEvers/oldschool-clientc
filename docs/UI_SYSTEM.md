# UI System Documentation

This UI system implements the RuneScape classic interface in C, based on the TypeScript client code from `clientts/src/client/Client.ts`.

## Overview

The UI system provides:
- Title screen with animated flames
- Login interface
- Game interface (viewport, minimap, sidebar, chatbox)
- Menu system
- Component system (buttons, textboxes, scrollbars)
- Chat system with message history
- Modal dialogs

## Files

- `src/osrs/ui.h` - Header file with structures and function declarations
- `src/osrs/ui.c` - Implementation of all UI functions
- `src/osrs/ui_integration_example.c` - Complete integration example

## Architecture

### UI States

The UI can be in one of four states:
- `UI_STATE_TITLE` - Title screen
- `UI_STATE_LOGIN` - Login interface
- `UI_STATE_GAME` - In-game interface
- `UI_STATE_LOADING` - Loading screen

### UI Context

The `UIContext` structure holds all UI state:
- Current UI state
- Login credentials
- Chat messages
- Menu state
- Sprites and fonts
- Mouse position
- Redraw flags

## Quick Start

### 1. Initialize the UI

```c
#include "osrs/ui.h"

struct UIContext* ui = ui_new();
ui_set_state(ui, UI_STATE_TITLE);

// Set up fonts
ui->font_plain11 = game->pixfont_p11;
ui->font_plain12 = game->pixfont_p12;
ui->font_bold12 = game->pixfont_b12;

// Set up sprites
ui->sprite_invback = game->sprite_invback;
ui->sprite_chatback = game->sprite_chatback;
ui->sprite_mapback = game->sprite_mapback;
// ... etc

// Initialize flame animation for title screen
ui->flames = ui_flame_animation_new();
```

### 2. Update the UI (every frame)

```c
// Update animations
if (ui->state == UI_STATE_TITLE && ui->flames) {
    ui_flame_animation_update(ui->flames);
}

// Update mouse position
ui->mouse_x = game->mouse_x;
ui->mouse_y = game->mouse_y;
```

### 3. Draw the UI

```c
switch (ui->state) {
    case UI_STATE_TITLE:
        ui_draw_title_screen(ui, pixel_buffer, stride);
        break;
        
    case UI_STATE_LOGIN:
        ui_draw_login_screen(ui, pixel_buffer, stride);
        break;
        
    case UI_STATE_GAME:
        ui_draw_game_frame(ui, pixel_buffer, stride);
        
        // Draw 3D scene here (in viewport area)
        
        ui_draw_minimap(ui, pixel_buffer, stride, player_x, player_z, camera_yaw);
        ui_draw_sidebar(ui, pixel_buffer, stride);
        ui_draw_chatbox(ui, pixel_buffer, stride);
        
        if (ui->menu_visible) {
            ui_draw_menu(ui, pixel_buffer, stride);
        }
        break;
}
```

### 4. Handle Input

```c
// Mouse clicks
void handle_mouse_click(int x, int y, int button) {
    ui_handle_mouse_click(ui, x, y, button);
}

// Mouse movement
void handle_mouse_move(int x, int y) {
    ui_handle_mouse_move(ui, x, y);
}

// Keyboard
void handle_key_press(int keycode, char character) {
    ui_handle_key_press(ui, keycode, character);
}
```

### 5. Cleanup

```c
ui_free(ui);
```

## Features

### Title Screen

The title screen features:
- Animated flames on both sides using procedural fire effect
- Title text
- Login box with username/password fields

```c
ui_set_state(ui, UI_STATE_TITLE);
ui->flames = ui_flame_animation_new();
```

### Login Interface

The login interface provides:
- Username field
- Password field (masked)
- Login button
- Error/status messages
- Tab key to switch fields
- Enter key to submit

### Game Interface

The game interface consists of:

#### Viewport (512x334)
- 3D scene rendering area
- Located at top-left of screen

#### Minimap (213x203)
- Circular minimap display
- Compass indicator
- Player dot at center
- Located at top-right

#### Sidebar (210x293)
- Inventory/stats/prayer/etc.
- Tab icons at bottom
- Located at right side

#### Chatbox (519x165)
- Message history (up to 100 messages)
- Chat input field
- System messages
- Player messages
- Private messages
- Located at bottom

### Chat System

```c
// Add system message
ui_chat_add_message(ui, "Welcome!", NULL, 0);

// Add player message
ui_chat_add_message(ui, "Hello world!", "Player1", 1);

// Clear all messages
ui_chat_clear(ui);
```

Message types:
- `0` - System message (black text)
- `1` - Public chat (player name + message in blue)
- `2` - Public chat (same as 1)
- `3` - Private message

### Menu System

Right-click context menus:

```c
// Show menu
ui->menu_visible = true;
ui->menu_x = x;
ui->menu_y = y;
ui->menu_size = 3;
ui->menu_options[0] = strdup("Walk here");
ui->menu_options[1] = strdup("Examine");
ui->menu_options[2] = strdup("Cancel");

// Calculate dimensions
ui->menu_width = 150;
ui->menu_height = ui->menu_size * 15 + 5;
```

### Modal Dialogs

```c
ui->modal_message_visible = true;
strncpy(ui->modal_message, "Are you sure?", sizeof(ui->modal_message));
```

### UI Components

#### Button

```c
ui_draw_button(
    pixel_buffer, stride,
    x, y, width, height,
    "Button Text",
    font,
    pressed,  // bool
    hovered   // bool
);
```

#### Textbox

```c
ui_draw_textbox(
    pixel_buffer, stride,
    x, y, width, height,
    "text content",
    font,
    focused  // bool - shows cursor
);
```

#### Scrollbar

```c
ui_draw_scrollbar(
    pixel_buffer, stride,
    x, y, height,
    scroll_position,
    scroll_height
);
```

### 2D Drawing Primitives

```c
// Line
ui_draw_line(pixel_buffer, stride, x0, y0, x1, y1, color_rgb);

// Rectangle outline
ui_draw_rect_outline(pixel_buffer, stride, x, y, width, height, color_rgb);

// Circle
ui_draw_circle(pixel_buffer, stride, center_x, center_y, radius, color_rgb);

// Horizontal gradient
ui_draw_horizontal_gradient(
    pixel_buffer, stride,
    x, y, width, height,
    color_left, color_right
);

// Vertical gradient
ui_draw_vertical_gradient(
    pixel_buffer, stride,
    x, y, width, height,
    color_top, color_bottom
);
```

### Tooltips

```c
ui_draw_tooltip(ui, pixel_buffer, stride, "Tooltip text");
```

## Layout Constants

All UI layout constants are defined in `ui.h`:

```c
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
```

## Color Constants

Common colors are predefined:

```c
UI_COLOR_BLACK
UI_COLOR_WHITE
UI_COLOR_RED
UI_COLOR_GREEN
UI_COLOR_BLUE
UI_COLOR_YELLOW
UI_COLOR_CYAN
UI_COLOR_MAGENTA
UI_COLOR_GRAY
UI_COLOR_DARKGRAY
UI_COLOR_LIGHTGRAY
UI_COLOR_DARKBLUE
UI_COLOR_DARKGREEN
UI_COLOR_DARKRED
UI_COLOR_ORANGE
UI_COLOR_PURPLE
```

## Redraw Flags

To optimize rendering, use redraw flags:

```c
ui->redraw_frame = true;    // Redraw entire game frame
ui->redraw_sidebar = true;  // Redraw sidebar only
ui->redraw_chatback = true; // Redraw chatbox only
ui->redraw_minimap = true;  // Redraw minimap only
```

## Flame Animation

The title screen features procedural fire animation:

```c
// Create
struct UIFlameAnimation* flames = ui_flame_animation_new();

// Update (every frame)
ui_flame_animation_update(flames);

// Draw
ui_flame_animation_draw(flames, pixel_buffer, stride, x, y);

// Free
ui_flame_animation_free(flames);
```

The flame effect uses:
- Heat diffusion simulation
- Gradient coloring (red -> yellow -> white)
- Wave distortion for visual effect
- 128x256 pixel resolution

## Integration with Existing Code

### Sprites

The UI uses sprites from the game cache:
- `sprite_invback` - Inventory background
- `sprite_chatback` - Chatbox background
- `sprite_mapback` - Minimap background
- `sprite_backbase1/2` - Frame backgrounds
- `sprite_compass` - Compass indicator
- `sprite_sideicons[13]` - Tab icons
- `sprite_scrollbar0/1` - Scrollbar graphics

### Fonts

The UI uses fonts from the game cache:
- `font_plain11` - Small plain text
- `font_plain12` - Normal plain text
- `font_bold12` - Bold text
- `font_quill8` - Decorative text

### 3D Viewport Integration

The 3D scene should be rendered to the viewport area:

```c
// Set up viewport for 3D rendering
struct DashViewPort viewport = {
    .width = UI_VIEWPORT_WIDTH,
    .height = UI_VIEWPORT_HEIGHT,
    .x_center = UI_VIEWPORT_WIDTH / 2,
    .y_center = UI_VIEWPORT_HEIGHT / 2,
    .stride = UI_WIDTH
};

// Render 3D scene
dash3d_render_model(
    dash,
    model,
    position,
    &viewport,
    camera,
    pixel_buffer + UI_VIEWPORT_Y * UI_WIDTH + UI_VIEWPORT_X
);
```

## Best Practices

1. **Initialize once** - Create UI context during game startup
2. **Update every frame** - Call update functions each frame
3. **Use redraw flags** - Only redraw what changed
4. **Handle input** - Connect SDL events to UI input handlers
5. **Clean up** - Free UI context on shutdown
6. **Sprite management** - Load sprites once, reuse in UI
7. **Font management** - Load fonts once, reuse in UI

## Future Enhancements

Possible improvements:
- Interface/component config loading from cache
- Advanced text rendering (colors, effects)
- Scrollable containers
- Drag and drop for inventory
- Animation system for UI elements
- Sound effects integration
- Minimap rendering from scene data
- Full compass rotation support

## Troubleshooting

**Issue**: UI not drawing
- Check that fonts and sprites are loaded
- Verify pixel buffer and stride are correct
- Ensure UI state is set properly

**Issue**: Input not working
- Check mouse coordinates are being updated
- Verify input handlers are connected to SDL events
- Ensure UI state allows input (e.g., game state for game input)

**Issue**: Flames not animating
- Call `ui_flame_animation_update()` every frame
- Ensure flame animation was created
- Check that UI state is TITLE or LOGIN

**Issue**: Chat messages not showing
- Verify font is loaded
- Check message count and array
- Ensure redraw_chatback flag is set after adding messages

## Credits

Based on the TypeScript client code from the `clientts` folder, specifically `Client.ts` which implements the RuneScape game client interface.

## License

Same as the parent project.
