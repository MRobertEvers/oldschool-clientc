# Inventory Now Draws in Sidebar Area

## Changes Made

### 1. Positioned Inventory in Sidebar
The inventory is now drawn at position **553, 205** (the sidebar area) where the inventory background sprite (`invback`) is displayed.

### 2. Updated Render Loop
Added sidebar interface rendering:

```cpp
// Render sidebar interface (inventory area at 553, 205)
if( game->sidebar_interface_id != -1 )
{
    struct CacheDatConfigComponent* sidebar_component =
        buildcachedat_get_component(game->buildcachedat, game->sidebar_interface_id);
    if( sidebar_component )
    {
        interface_draw_component(
            game, sidebar_component, 553, 205, 0, renderer->pixel_buffer, renderer->width);
    }
}
```

### 3. Adjusted Inventory Layout
- **Position**: 0, 0 (relative to sidebar at 553, 205)
- **Size**: 190x261 pixels (matches sidebar dimensions)
- **Grid**: 4 columns × 7 rows = 28 slots
- **Margins**: 5px between slots
- **Removed background** to show the `invback` sprite underneath

### 4. Added Realistic Item IDs
The inventory now contains realistic OSRS item IDs:

| Slot | Item | ID | Count |
|------|------|-----|-------|
| 0 | Bronze dagger | 1205 | 1 |
| 1 | Coins | 995 | 10000 |
| 2 | Lobster | 379 | 15 |
| 3 | Fire runes | 554 | 500 |
| 5 | Rune pickaxe | 1275 | 1 |
| 10 | Sharks | 385 | 20 |
| 15 | Dragon scimitar | 4587 | 1 |
| 20 | Prayer potion | 2434 | 4 |

### 5. Updated ImGui Controls
Two separate buttons:
- **Toggle Example Interface** - Controls `viewport_interface_id` (center screen)
- **Toggle Inventory** - Controls `sidebar_interface_id` (right sidebar at 553, 205)

## Testing

```bash
cd build && make && ./osx
```

Click **"Toggle Inventory"** to show the inventory in the sidebar area (where the inventory background sprite is).

## Sidebar Position Reference

From `Client.ts`:
- Sidebar X: 553
- Sidebar Y: 205
- Sidebar Width: 190 (743 - 553)
- Sidebar Height: 261 (466 - 205)

The inventory is now rendered at exactly these coordinates, overlaying the `invback` sprite that's already being drawn there.
