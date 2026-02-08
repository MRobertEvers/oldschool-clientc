# Platform Integration Patch Guide

This guide shows how to integrate the UI system into the existing platform code.

## Option 1: Quick Test Integration (Minimal Changes)

Add these lines to `src/platforms/platform_impl2_osx_sdl2_renderer_soft3d.cpp`:

### 1. Add include at top of file (after other includes)

```cpp
extern "C" {
#include "osrs/ui.h"
}
```

### 2. Add global UI context after other globals

```cpp
static struct UIContext* g_ui = NULL;
```

### 3. Initialize UI in your init function

```cpp
// In platform_impl2_osx_sdl2_renderer_soft3d_init or similar:
g_ui = ui_new();
if (g_ui) {
    ui_set_state(g_ui, UI_STATE_TITLE);
    
    // Set fonts if available
    if (game->pixfont_p11) g_ui->font_plain11 = game->pixfont_p11;
    if (game->pixfont_p12) g_ui->font_plain12 = game->pixfont_p12;
    if (game->pixfont_b12) g_ui->font_bold12 = game->pixfont_b12;
    if (game->pixfont_q8) g_ui->font_quill8 = game->pixfont_q8;
    
    // Set sprites if available
    if (game->sprite_invback) g_ui->sprite_invback = game->sprite_invback;
    if (game->sprite_chatback) g_ui->sprite_chatback = game->sprite_chatback;
    if (game->sprite_mapback) g_ui->sprite_mapback = game->sprite_mapback;
    if (game->sprite_compass) g_ui->sprite_compass = game->sprite_compass;
    
    for (int i = 0; i < 13; i++) {
        if (game->sprite_sideicons[i]) {
            g_ui->sprite_sideicons[i] = game->sprite_sideicons[i];
        }
    }
    
    // Initialize flames for title screen
    g_ui->flames = ui_flame_animation_new();
    
    // Add welcome message
    ui_chat_add_message(g_ui, "Welcome to RuneScape!", NULL, 0);
}
```

### 4. Update UI in render loop

```cpp
// In your render function, before drawing:
if (g_ui) {
    // Update animations
    if (g_ui->state == UI_STATE_TITLE && g_ui->flames) {
        ui_flame_animation_update(g_ui->flames);
    }
    
    // Update mouse position
    g_ui->mouse_x = game->mouse_x;
    g_ui->mouse_y = game->mouse_y;
}
```

### 5. Draw UI in render loop

```cpp
// In your render function, after drawing 3D scene:
if (g_ui) {
    switch (g_ui->state) {
        case UI_STATE_TITLE:
            ui_draw_title_screen(g_ui, renderer->pixel_buffer, UI_WIDTH);
            break;
            
        case UI_STATE_LOGIN:
            ui_draw_login_screen(g_ui, renderer->pixel_buffer, UI_WIDTH);
            break;
            
        case UI_STATE_GAME:
            if (g_ui->redraw_frame) {
                ui_draw_game_frame(g_ui, renderer->pixel_buffer, UI_WIDTH);
                g_ui->redraw_frame = false;
            }
            
            // Your 3D scene draws to viewport area here
            
            if (g_ui->redraw_minimap) {
                ui_draw_minimap(g_ui, renderer->pixel_buffer, UI_WIDTH,
                    game->camera_world_x, game->camera_world_z, game->camera_yaw);
                g_ui->redraw_minimap = false;
            }
            
            if (g_ui->redraw_sidebar) {
                ui_draw_sidebar(g_ui, renderer->pixel_buffer, UI_WIDTH);
                g_ui->redraw_sidebar = false;
            }
            
            if (g_ui->redraw_chatback) {
                ui_draw_chatbox(g_ui, renderer->pixel_buffer, UI_WIDTH);
                g_ui->redraw_chatback = false;
            }
            
            if (g_ui->menu_visible) {
                ui_draw_menu(g_ui, renderer->pixel_buffer, UI_WIDTH);
            }
            break;
    }
}
```

### 6. Handle SDL events

```cpp
// In your SDL event loop:
switch (event.type) {
    case SDL_MOUSEBUTTONDOWN:
        if (g_ui) {
            ui_handle_mouse_click(g_ui, event.button.x, event.button.y, event.button.button);
        }
        break;
        
    case SDL_MOUSEMOTION:
        if (g_ui) {
            ui_handle_mouse_move(g_ui, event.motion.x, event.motion.y);
        }
        break;
        
    case SDL_KEYDOWN:
        if (g_ui) {
            char ch = 0;
            if (event.key.keysym.sym < 128) {
                ch = (char)event.key.keysym.sym;
            }
            ui_handle_key_press(g_ui, event.key.keysym.sym, ch);
            
            // Special keys for testing:
            if (event.key.keysym.sym == SDLK_F1) {
                ui_set_state(g_ui, UI_STATE_TITLE);
            }
            if (event.key.keysym.sym == SDLK_F2) {
                ui_set_state(g_ui, UI_STATE_LOGIN);
            }
            if (event.key.keysym.sym == SDLK_F3) {
                ui_set_state(g_ui, UI_STATE_GAME);
                g_ui->redraw_frame = true;
                g_ui->redraw_minimap = true;
                g_ui->redraw_sidebar = true;
                g_ui->redraw_chatback = true;
            }
            if (event.key.keysym.sym == SDLK_t) {
                // Toggle chat input
                g_ui->chat_input_open = !g_ui->chat_input_open;
                g_ui->redraw_chatback = true;
            }
        }
        break;
}
```

### 7. Cleanup on shutdown

```cpp
// In your cleanup/shutdown function:
if (g_ui) {
    ui_free(g_ui);
    g_ui = NULL;
}
```

## Option 2: Full Integration (Recommended)

For production use, follow the detailed example in `src/osrs/ui_integration_example.c`.

The example provides proper encapsulation with:
- `example_ui_init()` - Initialization
- `example_ui_update()` - Frame updates
- `example_ui_draw()` - Rendering
- `example_ui_handle_*()` - Input handling
- `example_ui_cleanup()` - Cleanup

Plus helper functions for:
- State transitions
- Chat messages
- Menus
- Modal dialogs

## Testing the UI

### Test Title Screen (F1)
1. Press F1 to switch to title screen
2. Verify flames animate on both sides
3. Check title text is visible
4. Verify login box is displayed

### Test Login Screen (F2)
1. Press F2 to switch to login screen
2. Click on username field
3. Type some text
4. Press Tab to switch to password field
5. Type some text (should show as asterisks)
6. Click login button (will show "Connecting..." message)

### Test Game Interface (F3)
1. Press F3 to switch to game view
2. Verify viewport (black area) is shown
3. Check minimap is visible in top-right
4. Check sidebar is visible on right side
5. Check chatbox is visible at bottom
6. Verify tab icons are clickable

### Test Chat (T key)
1. Press T to open chat input
2. Type a message
3. Press Enter to send
4. Message should appear in chat history

### Test State Persistence
1. Switch between states with F1/F2/F3
2. Chat messages should persist
3. Login text should persist
4. Tab selection should persist

## Debugging Tips

### UI not showing
- Check `g_ui` is not NULL
- Verify fonts and sprites are loaded
- Check pixel_buffer pointer is valid
- Verify stride is correct (should be UI_WIDTH = 765)

### Input not working
- Check SDL events are being processed
- Verify mouse coordinates are in screen space
- Check UI state allows input

### Flames not animating
- Verify `ui_flame_animation_update()` is called each frame
- Check `g_ui->flames` is not NULL
- Verify state is UI_STATE_TITLE

### Performance issues
- Use redraw flags to avoid redrawing unchanged areas
- Only update flames when on title screen
- Profile with existing profiling tools

## Next Steps

After basic integration works:

1. **Connect to network** - Wire up login to actual server
2. **Load interfaces** - Read interface configs from cache
3. **Add inventory** - Implement item display in sidebar
4. **Minimap tiles** - Render actual minimap from scene data
5. **Sound effects** - Add UI sound hooks
6. **Animations** - Add smooth transitions

## Common Modifications

### Change colors
Edit color constants in `src/osrs/ui.h`:
```c
#define UI_COLOR_BACKGROUND 0x4D4233  // Change this
```

### Change layout
Edit layout constants in `src/osrs/ui.h`:
```c
#define UI_VIEWPORT_WIDTH 512  // Change this
#define UI_VIEWPORT_HEIGHT 334  // Change this
```

### Add new messages
```c
ui_chat_add_message(g_ui, "Your message", NULL, 0);
```

### Show menu
```c
g_ui->menu_visible = true;
g_ui->menu_x = mouse_x;
g_ui->menu_y = mouse_y;
g_ui->menu_size = 3;
g_ui->menu_options[0] = strdup("Option 1");
g_ui->menu_options[1] = strdup("Option 2");
g_ui->menu_options[2] = strdup("Cancel");
g_ui->menu_width = 150;
g_ui->menu_height = g_ui->menu_size * 15 + 5;
```

## Complete Example File Location

See `src/osrs/ui_integration_example.c` for a complete, working example.

## Documentation

Full documentation in `docs/UI_SYSTEM.md`

## Support

If you encounter issues:
1. Check this integration guide
2. Review `docs/UI_SYSTEM.md`
3. Examine `src/osrs/ui_integration_example.c`
4. Verify all prerequisites (fonts, sprites) are loaded
