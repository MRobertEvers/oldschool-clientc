#include "interface.h"

#include "graphics/dash.h"
#include "obj_icon.h"
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

    // Handle non-layer components directly
    if( component->type != COMPONENT_TYPE_LAYER )
    {
        switch( component->type )
        {
        case COMPONENT_TYPE_RECT:
            interface_draw_component_rect(game, component, x, y, pixel_buffer, stride);
            break;
        case COMPONENT_TYPE_TEXT:
            interface_draw_component_text(game, component, x, y, pixel_buffer, stride);
            break;
        case COMPONENT_TYPE_GRAPHIC:
            interface_draw_component_graphic(game, component, x, y, pixel_buffer, stride);
            break;
        case COMPONENT_TYPE_INV:
            interface_draw_component_inv(game, component, x, y, pixel_buffer, stride);
            break;
        case COMPONENT_TYPE_MODEL:
            // TODO: Implement model rendering
            break;
        }
        return;
    }

    // Only process layer components that have children
    if( !component->children )
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

        struct CacheDatConfigComponent* child =
            buildcachedat_get_component(game->buildcachedat, child_id);

        if( !child )
            continue;

        childX += child->x;
        childY += child->y;

        // Render based on child type
        switch( child->type )
        {
        case COMPONENT_TYPE_LAYER:
            interface_draw_component_layer(
                game, child, childX, childY, child->scroll, pixel_buffer, stride);
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
            dash2d_fill_rect(
                pixel_buffer, stride, x, y, component->width, component->height, colour);
        }
        else
        {
            dash2d_draw_rect(
                pixel_buffer, stride, x, y, component->width, component->height, colour);
        }
    }
    else
    {
        int alpha = 256 - (component->alpha & 0xFF);
        if( component->fill )
        {
            dash2d_fill_rect_alpha(
                pixel_buffer, stride, x, y, component->width, component->height, colour, alpha);
        }
        else
        {
            dash2d_draw_rect_alpha(
                pixel_buffer, stride, x, y, component->width, component->height, colour, alpha);
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
    dashfont_draw_text(
        font, (uint8_t*)component->text, x, y + font->height2d, colour, pixel_buffer, stride);
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
    printf("DEBUG GRAPHIC: Getting sprite for %s\n", component->graphic);
    struct DashSprite* sprite =
        buildcachedat_get_component_sprite(game->buildcachedat, component->graphic);

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
    {
        // printf(
        //     "DEBUG INV: No slot data (invSlotObjId=%p, invSlotObjCount=%p)\n",
        //     (void*)component->invSlotObjId,
        //     (void*)component->invSlotObjCount);
        return;
    }

    // printf(
    //     "DEBUG INV: Drawing inventory at (%d, %d), size=%dx%d, marginX=%d, marginY=%d\n",
    //     x,
    //     y,
    //     component->width,
    //     component->height,
    //     component->marginX,
    //     component->marginY);

    int slot = 0;
    int items_drawn = 0;

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
                // Item IDs are stored as 1-indexed in invSlotObjId, subtract 1 for actual obj
                // lookup (matching Client.ts line 9458: const id: number = child.invSlotObjId[slot]
                // - 1;)
                int item_id = component->invSlotObjId[slot] - 1;
                int item_count = component->invSlotObjCount[slot];

                // printf(
                //     "DEBUG INV: Slot %d: item_id=%d (stored as %d), item_count=%d at (%d, %d)\n",
                //     slot,
                //     item_id,
                //     component->invSlotObjId[slot],
                //     item_count,
                //     slotX,
                //     slotY);

                // Get or generate the item icon sprite
                struct DashSprite* icon = obj_icon_get(game, item_id, item_count);

                if( icon )
                {
                    items_drawn++;

                    // Check if this item is selected (dim it) - Client.ts line 9514-9515
                    bool is_selected =
                        (game->selected_area != 0 && game->selected_item == slot &&
                         game->selected_interface == component->id);

                    if( is_selected )
                    {
                        // Draw with alpha (dimmed) - drawAlpha(128, ...)
                        dash2d_blit_sprite_alpha(
                            game->sys_dash,
                            icon,
                            game->iface_view_port,
                            slotX,
                            slotY,
                            128,
                            pixel_buffer);
                    }
                    else
                    {
                        // Draw normally
                        dash2d_blit_sprite(
                            game->sys_dash,
                            icon,
                            game->iface_view_port,
                            slotX,
                            slotY,
                            pixel_buffer);
                    }

                    // Draw item count if > 1
                    if( item_count > 1 && game->pixfont_p11 )
                    {
                        char count_str[32];

                        // Format count with K/M suffixes for large numbers
                        if( item_count >= 1000000 )
                        {
                            snprintf(count_str, sizeof(count_str), "%dM", item_count / 1000000);
                        }
                        else if( item_count >= 1000 )
                        {
                            snprintf(count_str, sizeof(count_str), "%dK", item_count / 1000);
                        }
                        else
                        {
                            snprintf(count_str, sizeof(count_str), "%d", item_count);
                        }

                        // Draw count text with shadow (black then yellow)
                        dashfont_draw_text(
                            game->pixfont_p11,
                            (uint8_t*)count_str,
                            slotX + 1,
                            slotY + 10,
                            0x000000,
                            pixel_buffer,
                            stride);
                        dashfont_draw_text(
                            game->pixfont_p11,
                            (uint8_t*)count_str,
                            slotX,
                            slotY + 9,
                            0xFFFF00,
                            pixel_buffer,
                            stride);
                    }
                }
            }
            else if( component->invSlotGraphic && slot < 20 && component->invSlotGraphic[slot] )
            {
                // Draw empty slot graphic if specified
                const char* graphic_name = component->invSlotGraphic[slot];
                // Validate the string pointer is not corrupted
                if( graphic_name )
                {
                    struct DashSprite* sprite =
                        buildcachedat_get_component_sprite(game->buildcachedat, graphic_name);
                    if( sprite )
                    {
                        dash2d_blit_sprite(
                            game->sys_dash,
                            sprite,
                            game->iface_view_port,
                            slotX,
                            slotY,
                            pixel_buffer);
                    }
                }
            }

            slot++;
        }
    }

    // printf("DEBUG INV: Total items drawn: %d\n", items_drawn);
}

int
interface_check_inv_click(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int mouse_x,
    int mouse_y)
{
    if( !component || !component->invSlotObjId || !component->invSlotObjCount )
    {
        printf("interface_check_inv_click: invalid component or no inv data\n");
        return -1;
    }

    printf(
        "interface_check_inv_click: checking grid %dx%d, margins=%d,%d at (%d,%d) for mouse "
        "(%d,%d)\n",
        component->width,
        component->height,
        component->marginX,
        component->marginY,
        x,
        y,
        mouse_x,
        mouse_y);

    int slot = 0;

    // Iterate through grid to find which slot was clicked
    for( int row = 0; row < component->height; row++ )
    {
        for( int col = 0; col < component->width; col++ )
        {
            // Calculate slot position
            int slotX = x + col * (component->marginX + 32);
            int slotY = y + row * (component->marginY + 32);

            // Apply slot-specific offsets (for first 20 slots)
            if( slot < 20 && component->invSlotOffsetX && component->invSlotOffsetY )
            {
                slotX += component->invSlotOffsetX[slot];
                slotY += component->invSlotOffsetY[slot];
            }

            // Check if mouse is within this slot (32x32)
            if( mouse_x >= slotX && mouse_x < slotX + 32 && mouse_y >= slotY &&
                mouse_y < slotY + 32 )
            {
                printf(
                    "Mouse hit slot %d at (%d,%d), has item: %d\n",
                    slot,
                    slotX,
                    slotY,
                    component->invSlotObjId[slot]);

                // Only return slot if it has an item
                if( component->invSlotObjId[slot] > 0 )
                {
                    return slot;
                }
                return -1;
            }

            slot++;
        }
    }

    printf("No slot hit\n");
    return -1;
}

void
interface_handle_inv_button(
    struct GGame* game,
    int action,
    int obj_id,
    int slot,
    int component_id)
{
    // Based on Client.ts lines 9012-9067
    // action codes: 602=button1, 596=button2, 22=button3, 892=button4, 415=button5

    uint8_t opcode = 0;

    // Map action to opcode (from ClientProt.ts)
    if( action == 602 )
    {
        opcode = 13; // INV_BUTTON1
        printf("INV_BUTTON1: obj=%d, slot=%d, component=%d\n", obj_id, slot, component_id);
    }
    else if( action == 596 )
    {
        opcode = 58; // INV_BUTTON2
        printf("INV_BUTTON2: obj=%d, slot=%d, component=%d\n", obj_id, slot, component_id);
    }
    else if( action == 22 )
    {
        opcode = 48; // INV_BUTTON3
        printf("INV_BUTTON3: obj=%d, slot=%d, component=%d\n", obj_id, slot, component_id);
    }
    else if( action == 892 )
    {
        opcode = 183; // INV_BUTTON4
        printf("INV_BUTTON4: obj=%d, slot=%d, component=%d\n", obj_id, slot, component_id);
    }
    else if( action == 415 )
    {
        opcode = 242; // INV_BUTTON5
        printf("INV_BUTTON5: obj=%d, slot=%d, component=%d\n", obj_id, slot, component_id);
    }
    else
    {
        printf("Unknown inventory action: %d\n", action);
        return;
    }

    // Send packet with Isaac cipher encryption (p1isaac)
    // Based on Client.ts out.p1isaac() and example at tori_rs_cycle.u.c:788-790
    uint32_t encrypted_opcode = (opcode + isaac_next(game->random_out)) & 0xff;
    game->outbound_buffer[game->outbound_size++] = encrypted_opcode;

    // Send payload: p2(obj_id) + p2(slot) + p2(component_id)
    // Based on Client.ts lines 9051-9053
    game->outbound_buffer[game->outbound_size++] = (obj_id >> 8) & 0xFF;
    game->outbound_buffer[game->outbound_size++] = obj_id & 0xFF;
    game->outbound_buffer[game->outbound_size++] = (slot >> 8) & 0xFF;
    game->outbound_buffer[game->outbound_size++] = slot & 0xFF;
    game->outbound_buffer[game->outbound_size++] = (component_id >> 8) & 0xFF;
    game->outbound_buffer[game->outbound_size++] = component_id & 0xFF;

    // Update selection state (Client.ts lines 9055-9066)
    game->selected_cycle = 0;
    game->selected_interface = component_id;
    game->selected_item = slot;
    game->selected_area = 2; // Sidebar area

    // Check if this component belongs to viewport or chat (would change area)
    // For now we assume it's in sidebar (area 2)
    // TODO: Check component->layer against game->viewport_interface_id and game->chat_interface_id
}
