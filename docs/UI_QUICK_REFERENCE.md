# UI Quick Reference Card

## 🚀 5-Minute Integration

```c
// 1. Include
#include "osrs/ui.h"

// 2. Initialize
struct UIContext* ui = ui_new();
ui->font_plain12 = game->pixfont_p12;
ui->font_bold12 = game->pixfont_b12;
ui->flames = ui_flame_animation_new();
ui_set_state(ui, UI_STATE_TITLE);

// 3. Update (each frame)
if (ui->flames) ui_flame_animation_update(ui->flames);

// 4. Draw (each frame)
ui_draw_title_screen(ui, pixels, 765);

// 5. Input
ui_handle_mouse_click(ui, x, y, button);
ui_handle_key_press(ui, keycode, character);

// 6. Cleanup
ui_free(ui);
```

## 📐 Constants

```c
UI_WIDTH = 765
UI_HEIGHT = 503

UI_VIEWPORT_X = 4, Y = 4, W = 512, H = 334
UI_MINIMAP_X = 550, Y = 4, W = 213, H = 203
UI_SIDEBAR_X = 553, Y = 205, W = 210, H = 293
UI_CHATBOX_X = 0, Y = 338, W = 519, H = 165
```

## 🎨 Colors

```c
UI_COLOR_BLACK, WHITE, RED, GREEN, BLUE
UI_COLOR_YELLOW, CYAN, MAGENTA
UI_COLOR_GRAY, DARKGRAY, LIGHTGRAY
UI_COLOR_DARKBLUE, DARKGREEN, DARKRED
UI_COLOR_ORANGE, PURPLE
```

## 🖼️ States

```c
UI_STATE_TITLE    // Title screen with flames
UI_STATE_LOGIN    // Login interface
UI_STATE_GAME     // In-game UI
UI_STATE_LOADING  // Loading screen
```

## 🎯 Common Tasks

### Switch UI State
```c
ui_set_state(ui, UI_STATE_GAME);
ui->redraw_frame = true;
```

### Add Chat Message
```c
ui_chat_add_message(ui, "text", "sender", type);
// type: 0=system, 1=public, 2=public, 3=private
```

### Show Menu
```c
ui->menu_visible = true;
ui->menu_x = x; ui->menu_y = y;
ui->menu_size = 3;
ui->menu_options[0] = strdup("Option 1");
ui->menu_options[1] = strdup("Option 2");
ui->menu_options[2] = strdup("Cancel");
ui->menu_width = 150;
ui->menu_height = 3 * 15 + 5;
```

### Show Modal
```c
ui->modal_message_visible = true;
strncpy(ui->modal_message, "Message", sizeof(ui->modal_message));
ui->redraw_chatback = true;
```

### Draw Primitives
```c
ui_draw_line(pixels, stride, x0, y0, x1, y1, color);
ui_draw_rect_outline(pixels, stride, x, y, w, h, color);
ui_draw_circle(pixels, stride, cx, cy, r, color);
ui_draw_horizontal_gradient(pixels, stride, x, y, w, h, c1, c2);
```

### Draw Components
```c
ui_draw_button(pixels, stride, x, y, w, h, "text", font, pressed, hovered);
ui_draw_textbox(pixels, stride, x, y, w, h, "text", font, focused);
ui_draw_scrollbar(pixels, stride, x, y, height, scroll_pos, scroll_h);
```

## ⚡ Optimization

```c
// Use redraw flags to avoid full redraws
ui->redraw_frame = true;    // Full frame
ui->redraw_sidebar = true;  // Sidebar only
ui->redraw_chatback = true; // Chatbox only
ui->redraw_minimap = true;  // Minimap only

// Check before redrawing
if (ui->redraw_sidebar) {
    ui_draw_sidebar(ui, pixels, stride);
    ui->redraw_sidebar = false;
}
```

## 🎮 Input Handling

```c
// Mouse
ui_handle_mouse_click(ui, x, y, button);  // 1=left, 2=middle, 3=right
ui_handle_mouse_move(ui, x, y);

// Keyboard
ui_handle_key_press(ui, keycode, character);
// keycode: SDL key code (e.g., SDLK_RETURN)
// character: ASCII char (0-127) or 0 for special keys

// Special keys in login:
// Tab = switch fields
// Enter = submit
// Backspace = delete char
```

## 🔥 Flame Animation

```c
// Create
struct UIFlameAnimation* flames = ui_flame_animation_new();

// Update (every frame, only when visible)
if (ui->state == UI_STATE_TITLE && flames) {
    ui_flame_animation_update(flames);
}

// Draw
ui_flame_animation_draw(flames, pixels, stride, x, y);

// Free
ui_flame_animation_free(flames);
```

## 📊 Typical Frame

```c
void render_frame(struct GGame* game, int* pixels) {
    // 1. Update UI
    if (ui->flames) ui_flame_animation_update(ui->flames);
    ui->mouse_x = game->mouse_x;
    ui->mouse_y = game->mouse_y;
    
    // 2. Draw based on state
    switch (ui->state) {
        case UI_STATE_GAME:
            // Draw frame backgrounds
            if (ui->redraw_frame) {
                ui_draw_game_frame(ui, pixels, 765);
                ui->redraw_frame = false;
            }
            
            // Draw 3D scene to viewport area
            // (your 3D rendering code here)
            
            // Draw UI overlays
            if (ui->redraw_minimap) {
                ui_draw_minimap(ui, pixels, 765, px, pz, yaw);
                ui->redraw_minimap = false;
            }
            if (ui->redraw_sidebar) {
                ui_draw_sidebar(ui, pixels, 765);
                ui->redraw_sidebar = false;
            }
            if (ui->redraw_chatback) {
                ui_draw_chatbox(ui, pixels, 765);
                ui->redraw_chatback = false;
            }
            if (ui->menu_visible) {
                ui_draw_menu(ui, pixels, 765);
            }
            break;
            
        case UI_STATE_TITLE:
            ui_draw_title_screen(ui, pixels, 765);
            break;
            
        case UI_STATE_LOGIN:
            ui_draw_login_screen(ui, pixels, 765);
            break;
    }
}
```

## 🐛 Debug Checklist

- [ ] `ui` is not NULL
- [ ] Fonts are loaded (check `ui->font_*`)
- [ ] Sprites are loaded (check `ui->sprite_*`)
- [ ] Pixel buffer is valid
- [ ] Stride is 765 (UI_WIDTH)
- [ ] State is set correctly
- [ ] SDL events are connected
- [ ] Update is called before draw
- [ ] Cleanup is called on exit

## 📚 Full Documentation

- **Complete Guide:** `docs/UI_SYSTEM.md`
- **Integration:** `docs/UI_INTEGRATION_PATCH.md`
- **Example Code:** `src/osrs/ui_integration_example.c`
- **Summary:** `docs/UI_IMPLEMENTATION_SUMMARY.md`

## 🎯 Common Patterns

### Login Flow
```c
// Start at title
ui_set_state(ui, UI_STATE_TITLE);

// User clicks login button -> go to login screen
ui_set_state(ui, UI_STATE_LOGIN);

// User enters credentials and clicks login
if (login_successful) {
    ui_set_state(ui, UI_STATE_GAME);
    ui->redraw_frame = true;
    ui_chat_add_message(ui, "Welcome!", NULL, 0);
}
```

### Game UI
```c
// Draw game UI
ui_draw_game_frame(ui, pixels, 765);

// User clicks tab
if (clicked_in_tab_area) {
    int tab = calculate_tab_from_click(x, y);
    ui->selected_tab = tab;
    ui->redraw_sidebar = true;
}

// User types in chat
if (ui->chat_input_open) {
    // Handle key presses
    // On Enter, send message
    ui_chat_add_message(ui, ui->chat_input, "Player", 1);
}
```

### Right-Click Menu
```c
// On right-click
if (button == 3) {  // Right mouse button
    const char* options[] = {"Walk here", "Examine", "Cancel"};
    
    ui->menu_visible = true;
    ui->menu_x = x;
    ui->menu_y = y;
    ui->menu_size = 3;
    for (int i = 0; i < 3; i++) {
        ui->menu_options[i] = strdup(options[i]);
    }
    ui->menu_width = 150;
    ui->menu_height = 3 * 15 + 5;
}

// On left-click (to select option or dismiss)
if (button == 1) {
    if (ui->menu_visible) {
        if (point_in_menu(x, y)) {
            int option = (y - ui->menu_y) / 15;
            // Handle option selection
        }
        ui->menu_visible = false;
    }
}
```

## 💡 Tips

1. **Load sprites early** - Load all UI sprites during initialization
2. **Use redraw flags** - Only redraw what changed
3. **Update before draw** - Always update animations before drawing
4. **Handle cleanup** - Always call ui_free() on exit
5. **Test all states** - Test Title, Login, and Game states
6. **Profile performance** - Use existing profiling tools
7. **Check bounds** - Validate mouse coordinates before use
8. **Free menu options** - Free strdup'd strings when done

## 🎓 Learning Path

1. Read `UI_README.md` for overview
2. Study `ui_integration_example.c` for patterns
3. Check `docs/UI_INTEGRATION_PATCH.md` for integration
4. Review `docs/UI_SYSTEM.md` for full details
5. Build and test with your platform code

## 🔗 Key Files

```
src/osrs/ui.h                          # Header (379 lines)
src/osrs/ui.c                          # Implementation (1084 lines)
src/osrs/ui_integration_example.c     # Example (377 lines)
docs/UI_SYSTEM.md                      # Docs (542 lines)
docs/UI_INTEGRATION_PATCH.md           # Guide (335 lines)
```

---

**Start:** Read `UI_README.md`

**Integrate:** Follow `docs/UI_INTEGRATION_PATCH.md`

**Reference:** Use `docs/UI_SYSTEM.md`
