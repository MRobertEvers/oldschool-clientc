#include "interface.h"

#include "graphics/dash.h"
#include "obj_icon.h"
#include "osrs/buildcachedat.h"
#include "osrs/minimenu_action.h"
#include "osrs/packetout.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "osrs/varp_varbit_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INV_MENU_MAX 24

/* Run component script and return result. Mirrors Client.ts getIfVar (10625-10765).
 * Uses varp_varbit_manager for opcodes 5 (pushvar), 7 (var*100/46875), 13 (testbit), 14 (push_varbit).
 * Other opcodes return 0 for now. */
int
interface_get_if_var(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int script_id)
{
    if( !component->scripts || script_id >= component->scripts_count )
        return -2;

    int* script = component->scripts[script_id];
    if( !script )
        return -1;

    int acc = 0;
    int pc = 0;
    int arithmetic = 0;

    struct VarPVarBitManager* mgr = &game->varp_varbit;

    while( 1 )
    {
        int register_val = 0;
        int next_arithmetic = 0;
        int opcode = script[pc++];

        if( opcode == 0 )
            return acc;

        switch( opcode )
        {
        case 5:
            /* pushvar {id} */
            register_val = varp_varbit_get_varp(mgr, script[pc++]);
            break;
        case 7:
            /* register = (var[id] * 100) / 46875 */
            register_val = (varp_varbit_get_varp(mgr, script[pc++]) * 100) / 46875;
            break;
        case 13:
        {
            /* testbit {varp} {bit: 0..31} */
            int varp_val = varp_varbit_get_varp(mgr, script[pc++]);
            int lsb = script[pc++];
            register_val = (varp_val & (1 << lsb)) ? 1 : 0;
            break;
        }
        case 14:
        {
            /* push_varbit {varbit} */
            register_val = varp_varbit_get_varbit(mgr, script[pc++]);
            break;
        }
        case 20:
            /* push_constant */
            register_val = script[pc++];
            break;
        default:
            /* Other opcodes: advance pc, register_val stays 0 */
            if( opcode == 1 || opcode == 2 || opcode == 3 || opcode == 6 )
                pc += 1;
            else if( opcode == 4 || opcode == 10 )
                pc += 2;
            else if( opcode == 15 || opcode == 16 || opcode == 17 )
                next_arithmetic = (opcode == 15) ? 1 : (opcode == 16) ? 2 : 3;
            /* 8,9,11,12,18,19: no operands */
            break;
        }

        if( next_arithmetic == 0 )
        {
            if( arithmetic == 0 )
                acc += register_val;
            else if( arithmetic == 1 )
                acc -= register_val;
            else if( arithmetic == 2 && register_val != 0 )
                acc = acc / register_val;
            else if( arithmetic == 3 )
                acc = acc * register_val;
            arithmetic = 0;
        }
        else
        {
            arithmetic = next_arithmetic;
        }
    }
}

/* Return whether component passes script comparator check. Mirrors Client.ts getIfActive (10592-10623). */
bool
interface_get_if_active(
    struct GGame* game,
    struct CacheDatConfigComponent* component)
{
    if( !component->scriptComparator || !component->scriptOperand )
        return false;

    /* scriptComparator count matches scripts_count (one compare per script) */
    int count = component->scripts_count;
    if( count <= 0 )
        return false;

    for( int i = 0; i < count; i++ )
    {
        if( !component->scriptOperand )
            return false;

        int value = interface_get_if_var(game, component, i);
        int operand = component->scriptOperand[i];
        int comp = component->scriptComparator[i];

        if( comp == 2 )
        {
            if( value >= operand )
                return false;
        }
        else if( comp == 3 )
        {
            if( value <= operand )
                return false;
        }
        else if( comp == 4 )
        {
            if( value == operand )
                return false;
        }
        else if( value != operand )
        {
            return false;
        }
    }
    return true;
}

/* Fill rect clipped to viewport to prevent scroll layers overdrawing parent. */
static void
fill_rect_clipped(
    struct DashViewPort* vp,
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb)
{
    int x0 = x < vp->clip_left ? vp->clip_left : x;
    int y0 = y < vp->clip_top ? vp->clip_top : y;
    int x1 = x + width > vp->clip_right ? vp->clip_right : x + width;
    int y1 = y + height > vp->clip_bottom ? vp->clip_bottom : y + height;
    if( x0 >= x1 || y0 >= y1 )
        return;
    for( int py = y0; py < y1; py++ )
        for( int px = x0; px < x1; px++ )
            pixel_buffer[py * stride + px] = color_rgb;
}

static void
fill_rect_alpha_clipped(
    struct DashViewPort* vp,
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb,
    int alpha)
{
    int x0 = x < vp->clip_left ? vp->clip_left : x;
    int y0 = y < vp->clip_top ? vp->clip_top : y;
    int x1 = x + width > vp->clip_right ? vp->clip_right : x + width;
    int y1 = y + height > vp->clip_bottom ? vp->clip_bottom : y + height;
    if( x0 >= x1 || y0 >= y1 )
        return;
    int r = (color_rgb >> 16) & 0xFF;
    int g = (color_rgb >> 8) & 0xFF;
    int b = color_rgb & 0xFF;
    for( int py = y0; py < y1; py++ )
        for( int px = x0; px < x1; px++ )
        {
            int idx = py * stride + px;
            int existing = pixel_buffer[idx];
            int er = (existing >> 16) & 0xFF, eg = (existing >> 8) & 0xFF, eb = existing & 0xFF;
            int nr = (r * (256 - alpha) + er * alpha) >> 8;
            int ng = (g * (256 - alpha) + eg * alpha) >> 8;
            int nb = (b * (256 - alpha) + eb * alpha) >> 8;
            pixel_buffer[idx] = (nr << 16) | (ng << 8) | nb;
        }
}

static void
draw_rect_clipped(
    struct DashViewPort* vp,
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int color_rgb)
{
    int x0 = x < vp->clip_left ? vp->clip_left : x;
    int x1 = x + width > vp->clip_right ? vp->clip_right : x + width;
    int y0 = y < vp->clip_top ? vp->clip_top : y;
    int y1 = y + height > vp->clip_bottom ? vp->clip_bottom : y + height;
    if( x0 >= x1 || y0 >= y1 )
        return;
    /* Top edge */
    if( y >= vp->clip_top && y < vp->clip_bottom )
        for( int px = x0; px < x1; px++ )
            pixel_buffer[y * stride + px] = color_rgb;
    /* Bottom edge */
    if( y + height - 1 >= vp->clip_top && y + height - 1 < vp->clip_bottom )
        for( int px = x0; px < x1; px++ )
            pixel_buffer[(y + height - 1) * stride + px] = color_rgb;
    /* Left edge */
    if( x >= vp->clip_left && x < vp->clip_right )
        for( int py = y0; py < y1; py++ )
            pixel_buffer[py * stride + x] = color_rgb;
    /* Right edge */
    if( x + width - 1 >= vp->clip_left && x + width - 1 < vp->clip_right )
        for( int py = y0; py < y1; py++ )
            pixel_buffer[py * stride + (x + width - 1)] = color_rgb;
}

/* Blit subsprite (src_w x src_h) to pixel_buffer at (dx, dy), clipped to clip rect.
 * Skips source pixels that are 0 (transparent). Used for blitting sprites etc. */
static void
blit_subsprite_clipped(
    const int* src,
    int src_w,
    int src_h,
    int* pixel_buffer,
    int stride,
    int dx,
    int dy,
    int clip_left,
    int clip_top,
    int clip_right,
    int clip_bottom)
{
    for( int sy = 0; sy < src_h; sy++ )
    {
        int py = dy + sy;
        if( py < clip_top || py >= clip_bottom )
            continue;
        for( int sx = 0; sx < src_w; sx++ )
        {
            int px = dx + sx;
            if( px < clip_left || px >= clip_right )
                continue;
            int c = src[sy * src_w + sx];
            if( c == 0 )
                continue;
            pixel_buffer[py * stride + px] = c;
        }
    }
}

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
        return MINIMENU_ACTION_CANCEL;

    /* No obj selected, no spell: same order as Client.ts 10064-10152 */
    /* 1. child.interactable: obj.iop op 4, 3 (Drop / op3); else if op===4 add Drop */
    if( child->interactable )
    {
        for( int op = 4; op >= 3; op-- )
        {
            if( obj->iop[op] )
            {
                actions[n++] = (op == 4) ? MINIMENU_ACTION_OPHELD5 : MINIMENU_ACTION_OPHELD4;
                if( n >= INV_MENU_MAX )
                    goto done;
            }
            else if( op == 4 )
            {
                actions[n++] = MINIMENU_ACTION_OPHELD5; /* Drop @lre@ obj.name */
                if( n >= INV_MENU_MAX )
                    goto done;
            }
        }
    }

    /* 2. child.usable: Use */
    if( child->usable )
    {
        actions[n++] = MINIMENU_ACTION_OPHELDT_START;
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
                    actions[n++] = MINIMENU_ACTION_OPHELD1;
                else if( op == 1 )
                    actions[n++] = MINIMENU_ACTION_OPHELD2;
                else
                    actions[n++] = MINIMENU_ACTION_OPHELD3;
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
                    actions[n++] = MINIMENU_ACTION_INV_BUTTON1;
                else if( op == 1 )
                    actions[n++] = MINIMENU_ACTION_INV_BUTTON2;
                else if( op == 2 )
                    actions[n++] = MINIMENU_ACTION_INV_BUTTON3;
                else if( op == 3 )
                    actions[n++] = MINIMENU_ACTION_INV_BUTTON4;
                else
                    actions[n++] = MINIMENU_ACTION_INV_BUTTON5;
                if( n >= INV_MENU_MAX )
                    goto done;
            }
        }
    }

    /* 5. Examine (always last added) */
    actions[n++] = MINIMENU_ACTION_OPHELD6;

done:
    if( n == 0 )
        return MINIMENU_ACTION_INV_BUTTON1;

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

    /* Client.ts drawInterface (9396): skip entire subtree if hide && not hovered */
    if( component->hide && component->id != game->current_hovered_interface_id )
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

            /* Match Client.ts drawInterface: set bounds to layer rect and draw directly
             * onto main buffer (no subsprite). This draws black (0) and all colours
             * correctly; subsprite+blit had to treat 0 as transparent. */
            int saved_left = view_port->clip_left;
            int saved_top = view_port->clip_top;
            int saved_right = view_port->clip_right;
            int saved_bottom = view_port->clip_bottom;
            dash2d_set_bounds(
                view_port, childX, childY, childX + child->width, childY + child->height);
            interface_draw_component(game, child, childX, childY, scroll_pos, pixel_buffer, stride);
            dash2d_set_bounds(view_port, saved_left, saved_top, saved_right, saved_bottom);

            if( child->scroll > child->height )
            {
                int sb_right = childX + child->width + 16;
                int saved_right_sb = view_port->clip_right;
                if( sb_right > saved_right_sb )
                    view_port->clip_right = sb_right;
                interface_draw_scrollbar(
                    game,
                    childX + child->width,
                    childY,
                    scroll_pos,
                    child->scroll,
                    child->height,
                    pixel_buffer,
                    stride);
                view_port->clip_right = saved_right_sb;
            }
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

/* Recursive hit-test for hover; mirrors Client.ts handleInterfaceInput (10004-10009).
 * Updates *out_hovered_id when a child has (overlayer >= 0 || overColour != 0) and contains
 * (mouse_x, mouse_y). Last hit in draw order wins (topmost). */
static void
find_hovered_interface_id_recursive(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int scroll_y,
    int mouse_x,
    int mouse_y,
    int* out_hovered_id)
{
    if( !component->children || !component->childX || !component->childY )
        return;
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

        if( (child->overlayer >= 0 || child->overColour != 0) && mouse_x >= childX &&
            mouse_y >= childY && mouse_x < childX + child->width &&
            mouse_y < childY + child->height )
        {
            *out_hovered_id = (child->overlayer >= 0) ? child->overlayer : child->id;
        }

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
            find_hovered_interface_id_recursive(
                game, child, childX, childY, scroll_pos, mouse_x, mouse_y, out_hovered_id);
        }
    }
}

/* Return the hovered component id for the given root and area (root_x, root_y).
 * Used to set game->current_hovered_interface_id before drawing. */
int
interface_find_hovered_interface_id(
    struct GGame* game,
    struct CacheDatConfigComponent* root,
    int root_x,
    int root_y,
    int mouse_x,
    int mouse_y)
{
    if( !root || root->type != COMPONENT_TYPE_LAYER )
        return -1;
    int id = -1;
    find_hovered_interface_id_recursive(game, root, root_x, root_y, 0, mouse_x, mouse_y, &id);
    return id;
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
                    game,
                    child,
                    childX,
                    childY,
                    scroll_pos,
                    mouse_x,
                    mouse_y,
                    out_scrollbar_y,
                    out_height,
                    out_scroll_height);
                if( hit >= 0 )
                    return hit;
            }
        }
    }

    if( component->type == COMPONENT_TYPE_LAYER && component->scroll > component->height )
    {
        int sb_x = x + component->width;
        if( mouse_x >= sb_x && mouse_x < sb_x + 16 && mouse_y >= y &&
            mouse_y < y + component->height )
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
        game,
        root,
        root_x,
        root_y,
        0,
        mouse_x,
        mouse_y,
        out_scrollbar_y,
        out_height,
        out_scroll_height);
}

/* Client.ts CC_LOGOUT = 205. Find component with clientCode at (mouse_x, mouse_y).
 * Recursively walks layer children; returns topmost (last in draw order) component that has
 * clientCode > 0 and contains the point. Sets *out_component_id, *out_client_code, and
 * menu params (Client.ts menuParamA/B/C). For interface buttons: a=0, b=0, c=component_id.
 * Returns 1 if found, 0 else. */
static int
find_button_click_at_recursive(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int scroll_y,
    int mouse_x,
    int mouse_y,
    int* out_component_id,
    int* out_client_code,
    int* out_menu_param_a,
    int* out_menu_param_b,
    int* out_menu_param_c)
{
    int found = 0;
    if( !component->children || !component->childX || !component->childY )
        return 0;
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

        if( mouse_x >= childX && mouse_y >= childY && mouse_x < childX + child->width &&
            mouse_y < childY + child->height )
        {
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
                if( find_button_click_at_recursive(
                        game,
                        child,
                        childX,
                        childY,
                        scroll_pos,
                        mouse_x,
                        mouse_y,
                        out_component_id,
                        out_client_code,
                        out_menu_param_a,
                        out_menu_param_b,
                        out_menu_param_c) )
                {
                    found = 1;
                }
            }
            else if( child->clientCode > 0 )
            {
                *out_component_id = child_id;
                *out_client_code = child->clientCode;
                /* Client.ts addComponentOptions for IF_BUTTON: menuParamC = child.id only */
                if( out_menu_param_a )
                    *out_menu_param_a = 0;
                if( out_menu_param_b )
                    *out_menu_param_b = 0;
                if( out_menu_param_c )
                    *out_menu_param_c = child_id;
                found = 1;
            }
        }
    }
    return found;
}

int
interface_find_button_click_at(
    struct GGame* game,
    struct CacheDatConfigComponent* root,
    int root_x,
    int root_y,
    int mouse_x,
    int mouse_y,
    int* out_component_id,
    int* out_client_code,
    int* out_menu_param_a,
    int* out_menu_param_b,
    int* out_menu_param_c)
{
    if( !root || root->type != COMPONENT_TYPE_LAYER )
        return 0;
    return find_button_click_at_recursive(
        game,
        root,
        root_x,
        root_y,
        0,
        mouse_x,
        mouse_y,
        out_component_id,
        out_client_code,
        out_menu_param_a,
        out_menu_param_b,
        out_menu_param_c);
}

/* Client.ts handleScrollInput (9825-9831): up arrow scrollPosition -= dragCycles*4, down +=
 * dragCycles*4. step = rate (4) * game_cycles for hold scrolling. */
#define SCROLLBAR_ARROW_DELTA 4

void
interface_handle_scrollbar_arrow_step(
    struct GGame* game,
    int component_id,
    int max_scroll,
    int up_not_down,
    int step)
{
    if( component_id < 0 || component_id >= MAX_COMPONENT_SCROLL_IDS || step <= 0 )
        return;
    int pos = game->component_scroll_position[component_id];
    if( up_not_down )
    {
        pos -= step;
        if( pos < 0 )
            pos = 0;
    }
    else
    {
        pos += step;
        if( max_scroll > 0 && pos > max_scroll )
            pos = max_scroll;
    }
    game->component_scroll_position[component_id] = pos;
}

void
interface_handle_scrollbar_arrow(
    struct GGame* game,
    int component_id,
    int max_scroll,
    int up_not_down)
{
    interface_handle_scrollbar_arrow_step(
        game, component_id, max_scroll, up_not_down, SCROLLBAR_ARROW_DELTA);
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
    /* Map click Y (screen) to position along track: track goes from scrollbar_y+16 to
     * scrollbar_y+height-16 */
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
    /* Client.ts 10278-10290: getIfActive -> colour2/activeColour, else colour */
    int colour = component->colour;
    if( component->scriptComparator && interface_get_if_active(game, component) )
        colour = component->activeColour;

    struct DashViewPort* vp = game->iface_view_port;

    if( component->alpha == 0 )
    {
        if( component->fill )
            fill_rect_clipped(
                vp, pixel_buffer, stride, x, y, component->width, component->height, colour);
        else
            draw_rect_clipped(
                vp, pixel_buffer, stride, x, y, component->width, component->height, colour);
    }
    else
    {
        int alpha = 256 - (component->alpha & 0xFF);
        if( component->fill )
            fill_rect_alpha_clipped(
                vp, pixel_buffer, stride, x, y, component->width, component->height, colour, alpha);
        else
            draw_rect_clipped(
                vp, pixel_buffer, stride, x, y, component->width, component->height, colour);
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

    /* Client.ts 10315-10330: getIfActive -> colour2/activeColour + text2/activeText, else colour */
    int colour = component->colour;
    const char* text_src = component->text;
    if( component->scriptComparator && interface_get_if_active(game, component) )
    {
        colour = component->activeColour;
        if( component->activeText && component->activeText[0] != '\0' )
            text_src = component->activeText;
    }

    if( !text_src )
        return;

    /* Client.ts 9614-9681: first line at childY + font.height2d, then lineY += font.height2d per
     * line */
    int line_y = y + font->height2d;
    const char* rest = text_src;
    char line_buf[512];
    int line_buf_size = (int)sizeof(line_buf);

    struct DashViewPort* vp = game->iface_view_port;
    int cl = vp->clip_left;
    int ct = vp->clip_top;
    int cr = vp->clip_right;
    int cb = vp->clip_bottom;

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

            /* Client.ts 10356-10395: substitute %1 .. %5 with getIfVar(component, 0..4) */
            char expanded_buf[512];
            int exp_len = 0;
            for( int i = 0; i < line_len && exp_len < 511; i++ )
            {
                if( line_buf[i] == '%' && i + 1 < line_len )
                {
                    int script_idx = line_buf[i + 1] - '1';
                    if( script_idx >= 0 && script_idx <= 4 )
                    {
                        int val = interface_get_if_var(game, component, script_idx);
                        char val_buf[16];
                        int n;
                        if( val >= 999999999 )
                        {
                            val_buf[0] = '*';
                            val_buf[1] = '\0';
                            n = 1;
                        }
                        else
                        {
                            n = snprintf(val_buf, sizeof(val_buf), "%d", val);
                        }
                        for( int j = 0; j < n && val_buf[j] && exp_len < 511; j++ )
                            expanded_buf[exp_len++] = val_buf[j];
                        i++;
                        continue;
                    }
                }
                expanded_buf[exp_len++] = line_buf[i];
            }
            expanded_buf[exp_len] = '\0';

            int draw_x;
            if( component->center )
            {
                int text_w = dashfont_text_width(font, (uint8_t*)expanded_buf);
                draw_x = x + (component->width / 2) - (text_w / 2);
            }
            else
            {
                draw_x = x;
            }

            /* Client.ts PixFont.drawStringTaggable does y -= this.height2d before drawing (line
             * 150) */
            int draw_y = line_y - font->height2d;
            if( component->shadowed )
            {
                dashfont_draw_text_clipped(
                    font,
                    (uint8_t*)expanded_buf,
                    draw_x + 1,
                    draw_y + 1,
                    0x000000,
                    pixel_buffer,
                    stride,
                    cl,
                    ct,
                    cr,
                    cb);
            }
            dashfont_draw_text_clipped(
                font,
                (uint8_t*)expanded_buf,
                draw_x,
                draw_y,
                colour,
                pixel_buffer,
                stride,
                cl,
                ct,
                cr,
                cb);
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
    /* Client.ts drawScrollbar: 16px wide; top/bottom 16px = arrows; track y+16 to y+height-16 */
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

    struct DashViewPort* vp = game->iface_view_port;
    int cl = vp->clip_left;
    int ct = vp->clip_top;
    int cr = vp->clip_right;
    int cb = vp->clip_bottom;

    /* Arrows: clientts/src/client/Client.ts drawScrollbar (9767-9769) imageScrollbar0 at (x,y),
     * imageScrollbar1 at (x, y+height-16); loaded from scrollbar archive 0/1 (811-812) */
    if( game->sprite_scrollbar0 )
        dash2d_blit_sprite(game->sys_dash, game->sprite_scrollbar0, vp, x, y, pixel_buffer);
    if( game->sprite_scrollbar1 )
        dash2d_blit_sprite(
            game->sys_dash, game->sprite_scrollbar1, vp, x, y + height - 16, pixel_buffer);

    /* Draw track (y+16, track_h) clipped to viewport */
    int track_y = y + 16;
    int draw_y0 = track_y < ct ? ct : track_y;
    int draw_y1 = track_y + track_h;
    if( draw_y1 > cb )
        draw_y1 = cb;
    for( int py = draw_y0; py < draw_y1; py++ )
        for( int px = x; px < x + 16 && px < cr; px++ )
            if( px >= cl )
                pixel_buffer[py * stride + px] = 0x4D4233;

    /* Draw grip (y+16+grip_y, grip_size) clipped to viewport */
    int grip_y0 = y + 16 + grip_y;
    int grip_y1 = grip_y0 + grip_size;
    draw_y0 = grip_y0 < ct ? ct : grip_y0;
    draw_y1 = grip_y1 > cb ? cb : grip_y1;
    for( int py = draw_y0; py < draw_y1; py++ )
        for( int px = x; px < x + 16 && px < cr; px++ )
            if( px >= cl )
                pixel_buffer[py * stride + px] = 0x6D6253;
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
    if( action == MINIMENU_ACTION_INV_BUTTON1 )
    {
        opcode = PKTOUT_LC245_2_INV_BUTTON1; // INV_BUTTON1
        printf("INV_BUTTON1: obj=%d, slot=%d, component=%d\n", obj_id, slot, component_id);
    }
    else if( action == MINIMENU_ACTION_INV_BUTTON2 )
    {
        opcode = PKTOUT_LC245_2_INV_BUTTON2; // INV_BUTTON2
        printf("INV_BUTTON2: obj=%d, slot=%d, component=%d\n", obj_id, slot, component_id);
    }
    else if( action == MINIMENU_ACTION_INV_BUTTON3 )
    {
        opcode = PKTOUT_LC245_2_INV_BUTTON3; // INV_BUTTON3
        printf("INV_BUTTON3: obj=%d, slot=%d, component=%d\n", obj_id, slot, component_id);
    }
    else if( action == MINIMENU_ACTION_INV_BUTTON4 )
    {
        opcode = PKTOUT_LC245_2_INV_BUTTON4; // INV_BUTTON4
        printf("INV_BUTTON4: obj=%d, slot=%d, component=%d\n", obj_id, slot, component_id);
    }
    else if( action == MINIMENU_ACTION_INV_BUTTON5 )
    {
        opcode = PKTOUT_LC245_2_INV_BUTTON5; // INV_BUTTON5
        printf("INV_BUTTON5: obj=%d, slot=%d, component=%d\n", obj_id, slot, component_id);
    }
    /* Object options (obj.iop): Wield, Wear, etc. - Client.ts 405/38/422/478/347 block */
    else if( action == MINIMENU_ACTION_OPHELD1 )
    {
        opcode = PKTOUT_LC245_2_OPHELD1; // OPHELD1
    }
    else if( action == MINIMENU_ACTION_OPHELD2 )
    {
        opcode = PKTOUT_LC245_2_OPHELD2; // OPHELD2
    }
    else if( action == MINIMENU_ACTION_OPHELD3 )
    {
        opcode = PKTOUT_LC245_2_OPHELD3; // OPHELD3
    }
    else if( action == MINIMENU_ACTION_OPHELD4 )
    {
        opcode = PKTOUT_LC245_2_OPHELD4; // OPHELD4
    }
    else if( action == MINIMENU_ACTION_OPHELD5 )
    {
        opcode = PKTOUT_LC245_2_OPHELD5; // OPHELD5 (e.g. Drop)
    }
    else if( action == MINIMENU_ACTION_OPHELDT )
    {
        /* Use: select item for use-with; no packet (Client.ts 8927-8936) */
        return;
    }
    else if( action == MINIMENU_ACTION_IF_BUTTON )
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
