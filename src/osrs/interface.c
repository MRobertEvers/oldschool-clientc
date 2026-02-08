#include "interface.h"

#include "graphics/dash.h"
#include "obj_icon.h"
#include "osrs/buildcachedat.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/config_obj.h"

#include <stdio.h>
#include <string.h>

#define INV_MENU_MAX 24

/* Build menu options in same order as Client.ts handleInterfaceInput (inv slot),
 * then sort exactly as Client.ts (2498-2522): swap when [i] < 1000 && [i+1] > 1000
 * so primaries move right. Left-click uses useMenuOption(menuSize - 1) so default
 * is the last option after sort = actions[n-1].
 */
int
interface_get_inv_default_action(
    struct GGame* game,
    struct CacheDatConfigComponent* child,
    int obj_id,
    int slot)
{
    struct CacheDatConfigObj* obj = buildcachedat_get_obj(game->buildcachedat, obj_id);
    int actions[INV_MENU_MAX];
    int n = 0;

    if( !obj )
        return 602;

    /* No obj selected, no spell: same order as Client.ts 10064-10152 */
    /* 1. child.interactable: obj.iop op 4, 3 (Drop / op3); else if op===4 add Drop */
    if( child->interactable )
    {
        for( int op = 4; op >= 3; op-- )
        {
            if( obj->iop[op] )
            {
                actions[n++] = (op == 4) ? 347 : 478;
                if( n >= INV_MENU_MAX )
                    goto done;
            }
            else if( op == 4 )
            {
                actions[n++] = 347; /* Drop @lre@ obj.name */
                if( n >= INV_MENU_MAX )
                    goto done;
            }
        }
    }

    /* 2. child.usable: Use */
    if( child->usable )
    {
        actions[n++] = 188;
        if( n >= INV_MENU_MAX )
            goto done;
    }

    /* 3. child.interactable && obj.iop: op 2, 1, 0 (e.g. Wield, Wear) */
    if( child->interactable )
    {
        for( int op = 2; op >= 0; op-- )
        {
            if( obj->iop[op] )
            {
                if( op == 0 )
                    actions[n++] = 405;
                else if( op == 1 )
                    actions[n++] = 38;
                else
                    actions[n++] = 422;
                if( n >= INV_MENU_MAX )
                    goto done;
            }
        }
    }

    /* 4. child.iop: component inventory options op 4 down to 0 */
    if( child->iop )
    {
        for( int op = 4; op >= 0; op-- )
        {
            if( child->iop[op] )
            {
                if( op == 0 )
                    actions[n++] = 602;
                else if( op == 1 )
                    actions[n++] = 596;
                else if( op == 2 )
                    actions[n++] = 22;
                else if( op == 3 )
                    actions[n++] = 892;
                else
                    actions[n++] = 415;
                if( n >= INV_MENU_MAX )
                    goto done;
            }
        }
    }

    /* 5. Examine (always last added) */
    actions[n++] = 1773;

done:
    if( n == 0 )
        return 602;

    /* Sort: same as Client.ts 2498-2522 - swap when [i] < 1000 && [i+1] > 1000 */
    for( ;; )
    {
        int done_sort = 1;
        for( int i = 0; i < n - 1; i++ )
        {
            if( actions[i] < 1000 && actions[i + 1] > 1000 )
            {
                int t = actions[i];
                actions[i] = actions[i + 1];
                actions[i + 1] = t;
                done_sort = 0;
            }
        }
        if( done_sort )
            break;
    }

    /* Left-click uses useMenuOption(menuSize - 1) -> last option after sort */
    return actions[n - 1];
}

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
        {
            /* Use scroll position (Client.ts scrollPosition), not total scroll height */
            int scroll_pos = 0;
            if( child_id >= 0 && child_id < MAX_COMPONENT_SCROLL_IDS )
                scroll_pos = game->component_scroll_position[child_id];
            int max_scroll = child->scroll - child->height;
            if( max_scroll < 0 )
                max_scroll = 0;
            if( scroll_pos > max_scroll )
                scroll_pos = max_scroll;
            if( scroll_pos < 0 )
                scroll_pos = 0;
            interface_draw_component_layer(
                game, child, childX, childY, scroll_pos, pixel_buffer, stride);
            if( child->scroll > child->height )
                interface_draw_scrollbar(
                    game,
                    childX + child->width,
                    childY,
                    scroll_pos,
                    child->scroll,
                    child->height,
                    pixel_buffer,
                    stride);
            break;
        }
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

/* Recursive hit-test for scrollbar; mirrors layer child loop in interface_draw_component.
 * Returns component id of scrollable layer whose scrollbar contains (mouse_x, mouse_y), or -1.
 * On hit, sets *out_scrollbar_y, *out_height, *out_scroll_height for that layer. */
static int
find_scrollbar_at_recursive(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int scroll_y,
    int mouse_x,
    int mouse_y,
    int* out_scrollbar_y,
    int* out_height,
    int* out_scroll_height)
{
    if( component->children && component->childX && component->childY )
    {
        for( int i = 0; i < component->children_count; i++ )
        {
            int child_id = component->children[i];
            int childX = component->childX[i] + x;
            int childY = component->childY[i] + y - scroll_y;

            struct CacheDatConfigComponent* child =
                buildcachedat_get_component(game->buildcachedat, child_id);
            if( !child )
                continue;

            childX += child->x;
            childY += child->y;

            if( child->type == COMPONENT_TYPE_LAYER )
            {
                int scroll_pos = 0;
                if( child_id >= 0 && child_id < MAX_COMPONENT_SCROLL_IDS )
                    scroll_pos = game->component_scroll_position[child_id];
                int max_scroll = child->scroll - child->height;
                if( max_scroll < 0 )
                    max_scroll = 0;
                if( scroll_pos > max_scroll )
                    scroll_pos = max_scroll;
                if( scroll_pos < 0 )
                    scroll_pos = 0;

                int hit = find_scrollbar_at_recursive(
                    game, child, childX, childY, scroll_pos,
                    mouse_x, mouse_y, out_scrollbar_y, out_height, out_scroll_height);
                if( hit >= 0 )
                    return hit;
            }
        }
    }

    if( component->type == COMPONENT_TYPE_LAYER && component->scroll > component->height )
    {
        int sb_x = x + component->width;
        if( mouse_x >= sb_x && mouse_x < sb_x + 16 &&
            mouse_y >= y && mouse_y < y + component->height )
        {
            *out_scrollbar_y = y;
            *out_height = component->height;
            *out_scroll_height = component->scroll;
            return component->id;
        }
    }
    return -1;
}

int
interface_find_scrollbar_at(
    struct GGame* game,
    struct CacheDatConfigComponent* root,
    int root_x,
    int root_y,
    int mouse_x,
    int mouse_y,
    int* out_scrollbar_y,
    int* out_height,
    int* out_scroll_height)
{
    if( !root || root->type != COMPONENT_TYPE_LAYER )
        return -1;
    return find_scrollbar_at_recursive(
        game, root, root_x, root_y, 0,
        mouse_x, mouse_y, out_scrollbar_y, out_height, out_scroll_height);
}

void
interface_handle_scrollbar_click(
    struct GGame* game,
    int component_id,
    int scrollbar_y,
    int height,
    int scroll_height,
    int click_y)
{
    int track_h = height - 32;
    if( track_h <= 0 )
        return;
    int max_scroll = scroll_height - height;
    if( max_scroll <= 0 )
        return;
    /* Map click Y (screen) to position along track: track goes from scrollbar_y+16 to scrollbar_y+height-16 */
    int local_y = click_y - (scrollbar_y + 16);
    if( local_y < 0 )
        local_y = 0;
    if( local_y > track_h )
        local_y = track_h;
    int new_pos = (int)((long)max_scroll * (long)local_y / (long)track_h);
    if( new_pos < 0 )
        new_pos = 0;
    if( new_pos > max_scroll )
        new_pos = max_scroll;
    if( component_id >= 0 && component_id < MAX_COMPONENT_SCROLL_IDS )
        game->component_scroll_position[component_id] = new_pos;
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
    /* Client.ts 9614-9681: first line at childY + font.height2d, then lineY += font.height2d per
     * line */
    int line_y = y + font->height2d;
    const char* rest = component->text;
    char line_buf[512];
    int line_buf_size = (int)sizeof(line_buf);

    while( rest[0] != '\0' )
    {
        /* Find end of line: Client.ts uses indexOf('\\n') -> backslash then 'n' */
        const char* line_end = rest;
        while( line_end[0] != '\0' && !(line_end[0] == '\\' && line_end[1] == 'n') )
            line_end++;
        int line_len = (int)(line_end - rest);
        if( line_len >= line_buf_size )
            line_len = line_buf_size - 1;
        if( line_len > 0 )
        {
            memcpy(line_buf, rest, (size_t)line_len);
            line_buf[line_len] = '\0';

            int draw_x;
            if( component->center )
            {
                int text_w = dashfont_text_width(font, (uint8_t*)line_buf);
                draw_x = x + (component->width / 2) - (text_w / 2);
            }
            else
            {
                draw_x = x;
            }

            if( component->shadowed )
            {
                dashfont_draw_text(
                    font,
                    (uint8_t*)line_buf,
                    draw_x + 1,
                    line_y + 1,
                    0x000000,
                    pixel_buffer,
                    stride);
            }
            dashfont_draw_text(
                font, (uint8_t*)line_buf, draw_x, line_y, colour, pixel_buffer, stride);
        }
        line_y += font->height2d;
        rest = (line_end[0] == '\\' && line_end[1] == 'n') ? line_end + 2 : line_end;
        if( rest[0] == '\0' )
            break;
    }
}

void
interface_draw_scrollbar(
    struct GGame* game,
    int x,
    int y,
    int scroll_pos,
    int scroll_height,
    int height,
    int* pixel_buffer,
    int stride)
{
    /* Client.ts drawScrollbar: 16px wide, track from y+16 to y+height-16, grip sized by ratio */
    if( scroll_height <= height )
        return;
    int track_h = height - 32;
    if( track_h <= 0 )
        return;
    int grip_size = (track_h * height) / scroll_height;
    if( grip_size < 8 )
        grip_size = 8;
    if( grip_size > track_h )
        grip_size = track_h;
    int range = scroll_height - height;
    int grip_y = range > 0 ? ((track_h - grip_size) * scroll_pos) / range : 0;
    if( grip_y < 0 )
        grip_y = 0;
    if( grip_y > track_h - grip_size )
        grip_y = track_h - grip_size;
    /* Track (Client.ts SCROLLBAR_TRACK) */
    dash2d_fill_rect(pixel_buffer, stride, x, y + 16, 16, track_h, 0x4D4233);
    /* Grip (Client.ts SCROLLBAR_GRIP) */
    dash2d_fill_rect(pixel_buffer, stride, x, y + 16 + grip_y, 16, grip_size, 0x6D6253);
    (void)game;
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
    // Based on Client.ts: INV_BUTTON1-5 (component iop) and OPHELD1-5 (object iop)
    // Component options: 602=INV_BUTTON1, 596=INV_BUTTON2, 22=INV_BUTTON3, 892=INV_BUTTON4,
    // 415=INV_BUTTON5 Object options:    405=OPHELD1, 38=OPHELD2, 422=OPHELD3, 478=OPHELD4,
    // 347=OPHELD5

    // Check if this component has inventory options (iop)
    struct CacheDatConfigComponent* component =
        buildcachedat_get_component(game->buildcachedat, component_id);

    if( component )
    {
        printf(
            "Component found: id=%d, type=%d, iop=%p\n",
            component->id,
            component->type,
            (void*)component->iop);

        if( component->iop )
        {
            printf("Component has inventory options:\n");
            for( int i = 0; i < 5; i++ )
            {
                if( component->iop[i] )
                    printf("  iop[%d] = %s\n", i, component->iop[i]);
            }
        }
        else
        {
            printf("WARNING: Component %d has NO inventory options (iop=NULL)!\n", component_id);
            printf("The server will likely discard this packet.\n");
        }
    }

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
    /* Object options (obj.iop): Wield, Wear, etc. - Client.ts 405/38/422/478/347 block */
    else if( action == 405 )
    {
        opcode = 104; // OPHELD1
    }
    else if( action == 38 )
    {
        opcode = 193; // OPHELD2
    }
    else if( action == 422 )
    {
        opcode = 115; // OPHELD3
    }
    else if( action == 478 )
    {
        opcode = 194; // OPHELD4
    }
    else if( action == 347 )
    {
        opcode = 9; // OPHELD5 (e.g. Drop)
    }
    else if( action == 188 )
    {
        /* Use: select item for use-with; no packet (Client.ts 8927-8936) */
        return;
    }
    else if( action == 1773 )
    {
        /* Examine: client-side only, no packet */
        return;
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
