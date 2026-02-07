// Example: Creating and displaying a simple interface programmatically

#include "osrs/game.h"
#include "osrs/buildcachedat.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include <stdlib.h>
#include <string.h>

// Helper function to create a simple test interface
void create_test_interface(struct GGame* game)
{
    // Create a root layer component (ID: 1000)
    struct CacheDatConfigComponent* root = malloc(sizeof(struct CacheDatConfigComponent));
    memset(root, 0, sizeof(struct CacheDatConfigComponent));
    
    root->id = 1000;
    root->layer = -1;
    root->type = COMPONENT_TYPE_LAYER;  // Type 0 - Container
    root->buttonType = -1;
    root->clientCode = 0;
    root->width = 500;
    root->height = 400;
    root->x = 100;
    root->y = 50;
    root->alpha = 0;
    root->hide = false;
    root->scroll = 0;
    
    // This layer has 3 children: background rect, title text, and a sprite
    root->children_count = 3;
    root->children = malloc(3 * sizeof(int));
    root->childX = malloc(3 * sizeof(int));
    root->childY = malloc(3 * sizeof(int));
    
    // Child 0: Background rectangle (ID: 1001)
    root->children[0] = 1001;
    root->childX[0] = 0;
    root->childY[0] = 0;
    
    // Child 1: Title text (ID: 1002)
    root->children[1] = 1002;
    root->childX[1] = 10;
    root->childY[1] = 10;
    
    // Child 2: Icon sprite (ID: 1003)
    root->children[2] = 1003;
    root->childX[2] = 450;
    root->childY[2] = 10;
    
    // Register the root component
    buildcachedat_add_component(game->buildcachedat, 1000, root);
    
    // Create child 0: Background rectangle
    struct CacheDatConfigComponent* bg_rect = malloc(sizeof(struct CacheDatConfigComponent));
    memset(bg_rect, 0, sizeof(struct CacheDatConfigComponent));
    
    bg_rect->id = 1001;
    bg_rect->layer = 1000;
    bg_rect->type = COMPONENT_TYPE_RECT;  // Type 3 - Rectangle
    bg_rect->buttonType = -1;
    bg_rect->width = 500;
    bg_rect->height = 400;
    bg_rect->x = 0;
    bg_rect->y = 0;
    bg_rect->fill = true;  // Filled rectangle
    bg_rect->colour = 0x332211;  // Dark brown color
    bg_rect->alpha = 200;  // Semi-transparent
    
    buildcachedat_add_component(game->buildcachedat, 1001, bg_rect);
    
    // Create child 1: Title text
    struct CacheDatConfigComponent* title_text = malloc(sizeof(struct CacheDatConfigComponent));
    memset(title_text, 0, sizeof(struct CacheDatConfigComponent));
    
    title_text->id = 1002;
    title_text->layer = 1000;
    title_text->type = COMPONENT_TYPE_TEXT;  // Type 4 - Text
    title_text->buttonType = -1;
    title_text->width = 480;
    title_text->height = 20;
    title_text->x = 0;
    title_text->y = 0;
    title_text->font = 2;  // Bold 12pt font (pixfont_b12)
    title_text->text = strdup("Test Interface");
    title_text->colour = 0xFFFF00;  // Yellow color
    title_text->center = false;
    title_text->shadowed = true;
    
    buildcachedat_add_component(game->buildcachedat, 1002, title_text);
    
    // Create child 2: Icon sprite (if you have a sprite loaded)
    struct CacheDatConfigComponent* icon = malloc(sizeof(struct CacheDatConfigComponent));
    memset(icon, 0, sizeof(struct CacheDatConfigComponent));
    
    icon->id = 1003;
    icon->layer = 1000;
    icon->type = COMPONENT_TYPE_GRAPHIC;  // Type 5 - Sprite
    icon->buttonType = -1;
    icon->width = 32;
    icon->height = 32;
    icon->x = 0;
    icon->y = 0;
    icon->graphic = strdup("compass");  // Use compass sprite as example
    
    buildcachedat_add_component(game->buildcachedat, 1003, icon);
    
    // Note: For the sprite to show, make sure compass sprite is registered:
    // buildcachedat_add_component_sprite(game->buildcachedat, "compass", game->sprite_compass);
}

// Usage in your game initialization or test code:
void test_interface_rendering(struct GGame* game)
{
    // 1. Create the test interface components
    create_test_interface(game);
    
    // 2. Register compass sprite for the icon component
    if (game->sprite_compass) {
        buildcachedat_add_component_sprite(
            game->buildcachedat, 
            "compass", 
            game->sprite_compass);
    }
    
    // 3. Display the interface by setting its ID
    game->viewport_interface_id = 1000;
    
    // The interface will now render automatically in the render loop!
    // You should see:
    // - A semi-transparent dark brown rectangle (500x400)
    // - Yellow text "Test Interface" in the top-left
    // - A compass icon in the top-right
}

// To hide the interface:
void hide_test_interface(struct GGame* game)
{
    game->viewport_interface_id = -1;
}

/*
 * INTEGRATION STEPS:
 * 
 * 1. Call create_test_interface(game) after game initialization
 * 2. Call test_interface_rendering(game) to display it
 * 3. The interface will render automatically in the main render loop
 * 4. Call hide_test_interface(game) to hide it
 * 
 * EXPECTED OUTPUT:
 * - A floating semi-transparent brown box at position (100, 50)
 * - Text "Test Interface" in yellow at the top
 * - Compass icon in the top-right corner
 * 
 * NEXT STEPS:
 * - Load actual interface definitions from cache files
 * - Parse component data from .dat files
 * - Implement inventory grid rendering (TYPE_INV)
 * - Add scroll bar support
 * - Handle mouse hover states
 */
