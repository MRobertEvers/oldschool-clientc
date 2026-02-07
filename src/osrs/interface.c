#include "interface.h"

#include "graphics/dash.h"
#include "osrs/buildcachedat.h"
#include "osrs/rscache/tables_dat/config_component.h"

#include <stdio.h>
#include <string.h>

void
interface_draw_component(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int scroll_y,
    int* pixel_buffer,
    int stride)
{
    if( !component )
        return;
        
    // Only process layer components (type 0) that have children
    if( component->type != COMPONENT_TYPE_LAYER || !component->children )
        return;
    
    // Save current bounds
    struct DashViewPort* view_port = game->iface_view_port;
    int saved_left = view_port->clip_left;
    int saved_top = view_port->clip_top;
    int saved_right = view_port->clip_right;
    int saved_bottom = view_port->clip_bottom;
    
    // Set bounds for this component
    dash2d_set_bounds(view_port, x, y, x + component->width, y + component->height);
    
    // Iterate through children
    for( int i = 0; i < component->children_count; i++ )
    {
        if( !component->childX || !component->childY )
            continue;
            
        int child_id = component->children[i];
        int childX = component->childX[i] + x;
        int childY = component->childY[i] + y - scroll_y;
        
        struct CacheDatConfigComponent* child = buildcachedat_get_component(
            game->buildcachedat, child_id);
            
        if( !child )
            continue;
            
        childX += child->x;
        childY += child->y;
        
        // Render based on child type
        switch( child->type )
        {
        case COMPONENT_TYPE_LAYER:
            interface_draw_component_layer(game, child, childX, childY, child->scroll, pixel_buffer, stride);
            break;
        case COMPONENT_TYPE_RECT:
            interface_draw_component_rect(game, child, childX, childY, pixel_buffer, stride);
            break;
        case COMPONENT_TYPE_TEXT:
            interface_draw_component_text(game, child, childX, childY, pixel_buffer, stride);
            break;
        case COMPONENT_TYPE_GRAPHIC:
            interface_draw_component_graphic(game, child, childX, childY, pixel_buffer, stride);
            break;
        case COMPONENT_TYPE_INV:
            interface_draw_component_inv(game, child, childX, childY, pixel_buffer, stride);
            break;
        case COMPONENT_TYPE_MODEL:
            // TODO: Implement model rendering
            break;
        }
    }
    
    // Restore bounds
    dash2d_set_bounds(view_port, saved_left, saved_top, saved_right, saved_bottom);
}

void
interface_draw_component_layer(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int scroll_y,
    int* pixel_buffer,
    int stride)
{
    // Recursive call to draw this layer and its children
    interface_draw_component(game, component, x, y, scroll_y, pixel_buffer, stride);
}

void
interface_draw_component_rect(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride)
{
    int colour = component->colour;
    
    if( component->alpha == 0 )
    {
        if( component->fill )
        {
            dash2d_fill_rect(pixel_buffer, stride, x, y, component->width, component->height, colour);
        }
        else
        {
            dash2d_draw_rect(pixel_buffer, stride, x, y, component->width, component->height, colour);
        }
    }
    else
    {
        int alpha = 256 - (component->alpha & 0xFF);
        if( component->fill )
        {
            dash2d_fill_rect_alpha(pixel_buffer, stride, x, y, component->width, component->height, colour, alpha);
        }
        else
        {
            dash2d_draw_rect_alpha(pixel_buffer, stride, x, y, component->width, component->height, colour, alpha);
        }
    }
}

void
interface_draw_component_text(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride)
{
    if( !component->text )
        return;
        
    // Get the font based on component->font
    struct DashPixFont* font = NULL;
    switch( component->font )
    {
    case 0:
        font = game->pixfont_p11;
        break;
    case 1:
        font = game->pixfont_p12;
        break;
    case 2:
        font = game->pixfont_b12;
        break;
    case 3:
        font = game->pixfont_q8;
        break;
    default:
        font = game->pixfont_p12;
        break;
    }
    
    if( !font )
        return;
        
    int colour = component->colour;
    
    // For now, just draw the text at the position
    // TODO: Handle center alignment, shadowed text, word wrapping, etc.
    dashfont_draw_text(font, (uint8_t*)component->text, x, y + font->height2d, colour, pixel_buffer, stride);
}

void
interface_draw_component_graphic(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride)
{
    if( !component->graphic )
        return;
        
    // Get the sprite from the cache
    struct DashSprite* sprite = buildcachedat_get_component_sprite(
        game->buildcachedat, component->graphic);
        
    if( !sprite )
        return;
        
    // Draw the sprite
    dash2d_blit_sprite(game->sys_dash, sprite, game->iface_view_port, x, y, pixel_buffer);
}

void
interface_draw_component_inv(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride)
{
    // Inventory components render items in a grid
    // Based on Client.ts lines 9438-9534
    
    if( !component->invSlotObjId || !component->invSlotObjCount )
        return;
        
    int slot = 0;
    
    // Iterate through grid: rows x cols
    for( int row = 0; row < component->height; row++ )
    {
        for( int col = 0; col < component->width; col++ )
        {
            // Calculate slot position
            // Each slot is 32x32 with margins
            int slotX = x + col * (component->marginX + 32);
            int slotY = y + row * (component->marginY + 32);
            
            // Apply slot-specific offsets (for first 20 slots)
            if( slot < 20 && component->invSlotOffsetX && component->invSlotOffsetY )
            {
                slotX += component->invSlotOffsetX[slot];
                slotY += component->invSlotOffsetY[slot];
            }
            
            // Draw item if present
            if( component->invSlotObjId[slot] > 0 )
            {
                // In the real implementation, this would:
                // 1. Get ObjType for the item ID
                // 2. Render the item icon (Pix32)
                // 3. Draw item count text overlay
                
                // For now, draw a placeholder colored square for each item
                int item_id = component->invSlotObjId[slot];
                int item_count = component->invSlotObjCount[slot];
                
                // Use item_id to determine color (simple hash)
                int color = 0x00FF00 | ((item_id * 12345) & 0xFF0000);
                
                // Draw 32x32 item placeholder
                dash2d_fill_rect_alpha(
                    pixel_buffer, stride, slotX, slotY, 32, 32, color, 200);
                
                // Draw item count if > 1
                if( item_count > 1 && game->pixfont_p11 )
                {
                    char count_str[32];
                    snprintf(count_str, sizeof(count_str), "%d", item_count);
                    
                    // Draw count text with shadow (black then yellow)
                    dashfont_draw_text(
                        game->pixfont_p11, (uint8_t*)count_str,
                        slotX + 1, slotY + 10, 0x000000, pixel_buffer, stride);
                    dashfont_draw_text(
                        game->pixfont_p11, (uint8_t*)count_str,
                        slotX, slotY + 9, 0xFFFF00, pixel_buffer, stride);
                }
            }
            else if( component->invSlotGraphic && slot < 20 )
            {
                // Draw empty slot graphic if specified
                const char* graphic_name = component->invSlotGraphic[slot];
                if( graphic_name )
                {
                    struct DashSprite* sprite = buildcachedat_get_component_sprite(
                        game->buildcachedat, graphic_name);
                    if( sprite )
                    {
                        dash2d_blit_sprite(
                            game->sys_dash, sprite, game->iface_view_port, slotX, slotY, pixel_buffer);
                    }
                }
            }
            
            slot++;
        }
    }
}

