#include "interface.h"

#include "osrs/clientscript_vm.h"
#include "osrs/interface_state.h"
#include "osrs/revconfig/uiscene.h"

#include "graphics/dash.h"
#include "obj_icon.h"
#include "osrs/buildcachedat.h"
#include "osrs/dash_utils.h"
#include "osrs/entity_scenebuild.h"
#include "osrs/minimenu_action.h"
#include "osrs/packetout.h"
#include "osrs/player_stats.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "osrs/varp_varbit_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INV_MENU_MAX 24

static struct DashPixFont*
interface_font_from_reftable(struct GGame* game, char const* name)
{
    if( !game || !game->ui_scene || !game->buildcachedat || !name )
        return NULL;
    int sid = buildcachedat_get_font_ref_id(game->buildcachedat, name);
    if( sid < 0 )
        return NULL;
    return uiscene_font_get(game->ui_scene, sid);
}

static struct DashSprite*
interface_component_sprite_from_reftable(struct GGame* game, char const* graphic_name)
{
    if( !game || !game->ui_scene || !game->buildcachedat || !graphic_name || !graphic_name[0] )
        return NULL;
    int eid = buildcachedat_get_component_sprite_element_id(game->buildcachedat, graphic_name);
    if( eid < 0 )
        return NULL;
    struct UISceneElement* el = uiscene_element_at(game->ui_scene, eid);
    if( !el || el->dash_sprites_count <= 0 || !el->dash_sprites )
        return NULL;
    return el->dash_sprites[0];
}

/* Run component script and return result. Delegates to ClientScriptVM. */
int
interface_get_if_var(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int script_id)
{
    if( !game || !game->clientscript_vm )
        return -2;
    return clientscript_vm_if_var(game->clientscript_vm, game, component, script_id);
}

bool
interface_get_if_active(
    struct GGame* game,
    struct CacheDatConfigComponent* component)
{
    if( !game || !game->clientscript_vm )
        return false;
    return clientscript_vm_if_active(game->clientscript_vm, game, component);
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
    if( !component || !game->iface )
        return;

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
            interface_draw_component_model(game, component, x, y, pixel_buffer, stride);
            break;
        default:
            break;
        }
        return;
    }

    if( !component->children )
        return;

    struct DashViewPort* view_port = game->iface_view_port;
    int saved_left = view_port->clip_left;
    int saved_top = view_port->clip_top;
    int saved_right = view_port->clip_right;
    int saved_bottom = view_port->clip_bottom;

    dash2d_set_bounds(view_port, x, y, x + component->width, y + component->height);

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

        switch( child->type )
        {
        case COMPONENT_TYPE_LAYER:
        {
            int scroll_pos = 0;
            if( child_id >= 0 && child_id < MAX_IFACE_SCROLL_IDS )
                scroll_pos = game->iface->component_scroll_position[child_id];
            int max_scroll = child->scroll - child->height;
            if( max_scroll < 0 )
                max_scroll = 0;
            if( scroll_pos > max_scroll )
                scroll_pos = max_scroll;
            if( scroll_pos < 0 )
                scroll_pos = 0;

            int sl = view_port->clip_left;
            int st = view_port->clip_top;
            int sr = view_port->clip_right;
            int sb = view_port->clip_bottom;
            dash2d_set_bounds(
                view_port, childX, childY, childX + child->width, childY + child->height);
            interface_draw_component(game, child, childX, childY, scroll_pos, pixel_buffer, stride);
            dash2d_set_bounds(view_port, sl, st, sr, sb);

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
            interface_draw_component_model(game, child, childX, childY, pixel_buffer, stride);
            break;
        default:
            break;
        }
    }

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
    interface_draw_component(game, component, x, y, scroll_y, pixel_buffer, stride);
}

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
    if( !game->iface )
        return;
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
            if( child_id >= 0 && child_id < MAX_IFACE_SCROLL_IDS )
                scroll_pos = game->iface->component_scroll_position[child_id];
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
    if( !game->iface )
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
    if( !game->iface )
        return -1;
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
                if( child_id >= 0 && child_id < MAX_IFACE_SCROLL_IDS )
                    scroll_pos = game->iface->component_scroll_position[child_id];
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

/* Client.ts addComponentOptions: buttons are clickable if buttonType (OK/TOGGLE/SELECT/CLOSE/
 * CONTINUE) or clientCode > 0. IF_BUTTON, TOGGLE_BUTTON, SELECT_BUTTON all send IF_BUTTON p2(c).
 * CLOSE_BUTTON sends CLOSE_MODAL. PAUSE_BUTTON sends RESUME_PAUSEBUTTON p2(c). */
static int
is_clickable_button(struct CacheDatConfigComponent* child)
{
    if( child->clientCode > 0 )
        return 1;
    /* Client.ts addComponentOptions: BUTTON_OK needs clientCode or buttonText. Config sets option
     * to "Ok" when empty. */
    if( child->buttonType == COMPONENT_BUTTON_TYPE_OK )
        return 1;
    if( child->buttonType == COMPONENT_BUTTON_TYPE_TOGGLE ||
        child->buttonType == COMPONENT_BUTTON_TYPE_SELECT )
        return 1;
    if( child->buttonType == COMPONENT_BUTTON_TYPE_CLOSE )
        return 1;
    if( child->buttonType == COMPONENT_BUTTON_TYPE_CONTINUE )
        return 1;
    return 0;
}

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
    int* out_button_action,
    int* out_menu_param_a,
    int* out_menu_param_b,
    int* out_menu_param_c)
{
    if( !game->iface )
        return 0;
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
                if( child_id >= 0 && child_id < MAX_IFACE_SCROLL_IDS )
                    scroll_pos = game->iface->component_scroll_position[child_id];
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
                        out_button_action,
                        out_menu_param_a,
                        out_menu_param_b,
                        out_menu_param_c) )
                {
                    found = 1;
                }
            }
            else if( is_clickable_button(child) )
            {
                *out_component_id = child_id;
                *out_client_code = child->clientCode;
                if( out_button_action )
                {
                    if( child->buttonType == COMPONENT_BUTTON_TYPE_CLOSE )
                        *out_button_action = IF_BUTTON_ACTION_CLOSE_MODAL;
                    else if( child->buttonType == COMPONENT_BUTTON_TYPE_CONTINUE )
                        *out_button_action = IF_BUTTON_ACTION_RESUME_PAUSEBUTTON;
                    else
                        *out_button_action = IF_BUTTON_ACTION_IF_BUTTON;
                }
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
    int* out_button_action,
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
        out_button_action,
        out_menu_param_a,
        out_menu_param_b,
        out_menu_param_c);
}

void
interface_apply_button_click_varp_optimistic(
    struct GGame* game,
    int component_id)
{
    struct CacheDatConfigComponent* com =
        buildcachedat_get_component(game->buildcachedat, component_id);
    if( !com )
        return;

    /* Client.ts doAction: scripts[0][0]==5 (pushvar) or [0]==14 (push_varbit).
     * Client only does optimistic update for opcode 5; we extend to 14 for varbit toggles. */
    if( !com->scripts || com->scripts_count < 1 || !com->scripts[0] )
        return;
    int* script = com->scripts[0];
    int opcode = script[0];

    if( opcode == 5 )
    {
        /* pushvar {id} */
        int varp = script[1];
        int current = varp_varbit_get_varp(&game->varp_varbit, varp);

        if( com->buttonType == COMPONENT_BUTTON_TYPE_TOGGLE )
        {
            varp_varbit_set_varp_optimistic(&game->varp_varbit, varp, 1 - current);
        }
        else if( com->buttonType == COMPONENT_BUTTON_TYPE_SELECT && com->scriptOperand )
        {
            int operand = com->scriptOperand[0];
            if( current != operand )
                varp_varbit_set_varp_optimistic(&game->varp_varbit, varp, operand);
        }
    }
    else if( opcode == 14 && com->buttonType == COMPONENT_BUTTON_TYPE_TOGGLE )
    {
        /* push_varbit {varbit} - toggle the varbit's bits in the base varp */
        int varbit_id = script[1];
        int current = varp_varbit_get_varbit(&game->varp_varbit, varbit_id);
        int new_val = 1 - current;

        struct VarPVarBitManager* mgr = &game->varp_varbit;
        if( varbit_id < 0 || varbit_id >= mgr->varbit_count )
            return;
        const struct VarBitType* vb = &mgr->varbit_types[varbit_id];
        if( vb->basevar < 0 || vb->basevar >= mgr->varp_count )
            return;

        int bit_count = vb->endbit - vb->startbit;
        if( bit_count <= 0 || bit_count >= VARP_VARBIT_READBIT_MAX )
            return;

        int mask = mgr->readbit[bit_count];
        int base_val = varp_varbit_get_varp(mgr, vb->basevar);
        int cleared = base_val & ~(mask << vb->startbit);
        int updated = cleared | ((new_val & mask) << vb->startbit);
        varp_varbit_set_varp_optimistic(mgr, vb->basevar, updated);
    }
    else if( opcode == 14 && com->buttonType == COMPONENT_BUTTON_TYPE_SELECT && com->scriptOperand )
    {
        /* push_varbit + SELECT: set varbit to operand */
        int varbit_id = script[1];
        int operand = com->scriptOperand[0];
        int current = varp_varbit_get_varbit(&game->varp_varbit, varbit_id);
        if( current == operand )
            return;

        struct VarPVarBitManager* mgr = &game->varp_varbit;
        if( varbit_id < 0 || varbit_id >= mgr->varbit_count )
            return;
        const struct VarBitType* vb = &mgr->varbit_types[varbit_id];
        if( vb->basevar < 0 || vb->basevar >= mgr->varp_count )
            return;

        int bit_count = vb->endbit - vb->startbit;
        if( bit_count <= 0 || bit_count >= VARP_VARBIT_READBIT_MAX )
            return;

        int mask = mgr->readbit[bit_count];
        int base_val = varp_varbit_get_varp(mgr, vb->basevar);
        int cleared = base_val & ~(mask << vb->startbit);
        int updated = cleared | ((operand & mask) << vb->startbit);
        varp_varbit_set_varp_optimistic(mgr, vb->basevar, updated);
    }
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
    if( !game->iface )
        return;
    if( component_id < 0 || component_id >= MAX_IFACE_SCROLL_IDS || step <= 0 )
        return;
    int pos = game->iface->component_scroll_position[component_id];
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
    game->iface->component_scroll_position[component_id] = pos;
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
    if( !game->iface )
        return;
    int track_h = height - 32;
    if( track_h <= 0 )
        return;
    int max_scroll = scroll_height - height;
    if( max_scroll <= 0 )
        return;
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
    if( component_id >= 0 && component_id < MAX_IFACE_SCROLL_IDS )
        game->iface->component_scroll_position[component_id] = new_pos;
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
    if( !game->iface )
        return;
    int colour = component->colour;
    bool active = component->scriptComparator && interface_get_if_active(game, component);
    if( active )
        colour = component->activeColour;
    bool hovered = (component->id == game->iface->current_hovered_interface_id);
    if( hovered )
    {
        if( active && component->activeOverColour != 0 )
            colour = component->activeOverColour;
        else if( !active && component->overColour != 0 )
            colour = component->overColour;
    }

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
                vp, pixel_buffer, stride, x, y, component->width, component->height, colour,
                alpha);
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
    if( !game->iface || !game->buildcachedat )
        return;
    static const char* font_names[] = { "p11", "p12", "b12", "q8" };
    int fidx = component->font;
    if( fidx < 0 || fidx > 3 )
        fidx = 1;
    struct DashPixFont* font = interface_font_from_reftable(game, font_names[fidx]);
    if( !font )
        return;

    int colour = component->colour;
    const char* text_src = component->text;
    bool active = component->scriptComparator && interface_get_if_active(game, component);
    if( active )
    {
        colour = component->activeColour;
        if( component->activeText && component->activeText[0] != '\0' )
            text_src = component->activeText;
    }
    bool hovered = (component->id == game->iface->current_hovered_interface_id);
    if( hovered )
    {
        if( active && component->activeOverColour != 0 )
            colour = component->activeOverColour;
        else if( !active && component->overColour != 0 )
            colour = component->overColour;
    }

    if( !text_src )
        return;

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

    struct DashSprite* sb0 =
        game->ui_scene ? uiscene_sprite_by_name(game->ui_scene, "scrollbar0", 0) : NULL;
    struct DashSprite* sb1 =
        game->ui_scene ? uiscene_sprite_by_name(game->ui_scene, "scrollbar1", 0) : NULL;
    if( sb0 )
        dash2d_blit_sprite(game->sys_dash, sb0, vp, x, y, pixel_buffer);
    if( sb1 )
        dash2d_blit_sprite(game->sys_dash, sb1, vp, x, y + height - 16, pixel_buffer);

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
    /* Client.ts 10417-10423: getIfActive -> graphic2/activeGraphic, else graphic */
    char const* graphic_name = component->graphic;
    if( component->scriptComparator && interface_get_if_active(game, component) &&
        component->activeGraphic && component->activeGraphic[0] != '\0' )
    {
        graphic_name = component->activeGraphic;
    }

    if( !graphic_name )
        return;

    struct DashSprite* sprite = interface_component_sprite_from_reftable(game, graphic_name);

    if( !sprite )
        return;

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
    if( !game->iface || !game->buildcachedat )
        return;
    if( !component->invSlotObjId || !component->invSlotObjCount )
        return;

    struct DashPixFont* p11 = interface_font_from_reftable(game, "p11");

    int slot = 0;
    for( int row = 0; row < component->height; row++ )
    {
        for( int col = 0; col < component->width; col++ )
        {
            int slotX = x + col * (component->marginX + 32);
            int slotY = y + row * (component->marginY + 32);

            if( slot < 20 && component->invSlotOffsetX && component->invSlotOffsetY )
            {
                slotX += component->invSlotOffsetX[slot];
                slotY += component->invSlotOffsetY[slot];
            }

            if( component->invSlotObjId[slot] > 0 )
            {
                int item_id = component->invSlotObjId[slot] - 1;
                int item_count = component->invSlotObjCount[slot];

                struct DashSprite* icon = obj_icon_get(game, item_id, item_count);

                if( icon )
                {
                    bool is_selected =
                        (game->iface->selected_area != 0 && game->iface->selected_item == slot &&
                         game->iface->selected_interface == component->id);

                    if( is_selected )
                    {
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
                        dash2d_blit_sprite(
                            game->sys_dash,
                            icon,
                            game->iface_view_port,
                            slotX,
                            slotY,
                            pixel_buffer);
                    }

                    if( item_count > 1 && p11 )
                    {
                        char count_str[32];
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

                        dashfont_draw_text(
                            p11,
                            (uint8_t*)count_str,
                            slotX + 1,
                            slotY + 10,
                            0x000000,
                            pixel_buffer,
                            stride);
                        dashfont_draw_text(
                            p11,
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
                const char* graphic_name = component->invSlotGraphic[slot];
                if( graphic_name )
                {
                    struct DashSprite* sprite =
                        interface_component_sprite_from_reftable(game, graphic_name);
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
}

void
interface_draw_component_model(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride)
{
    /* Client.ts: modelType 2 = NPC head, 3 = player head. Only support these for chat head. */
    if( component->modelType != 2 && component->modelType != 3 )
        return;

    int* slots = NULL;
    int* colors = NULL;
    if( component->modelType == 3 )
    {
        struct PlayerEntity* local = &game->world->players[ACTIVE_PLAYER_SLOT];
        if( !local->alive )
            return;
        slots = local->appearance.slots;
        colors = local->appearance.colors;
    }

    struct DashModel* head_model = entity_scenebuild_head_model_for_component(
        game, component->modelType, component->model, slots, colors);
    if( !head_model )
        return;

    /* Client.ts: MODEL components use component.anim (or activeAnim when active) for animation.
     * If component.anim is -1, fall back to NPC/player readyanim for chat heads. */
    int sequence_id = component->anim;
    if( sequence_id < 0 )
    {
        if( component->modelType == 2 )
        {
            struct CacheDatConfigNpc* npc =
                buildcachedat_get_npc(game->buildcachedat, component->model);
            if( npc )
                sequence_id = npc->readyanim;
        }
        else if( component->modelType == 3 )
        {
            struct PlayerEntity* local = &game->world->players[ACTIVE_PLAYER_SLOT];
            if( local->alive )
                sequence_id = local->animation.readyanim;
        }
    }
    struct CacheDatSequence* seq = NULL;
    if( sequence_id >= 0 && game->buildcachedat->sequences_hmap )
    {
        seq = buildcachedat_get_sequence(game->buildcachedat, sequence_id);
        if( seq && seq->frame_count > 0 && seq->frames )
        {
            int frame_i = (game->cycle / 5) % seq->frame_count;
            int frame_id = seq->frames[frame_i];
            struct CacheAnimframe* animframe =
                buildcachedat_get_animframe(game->buildcachedat, frame_id);
            if( animframe )
            {
                struct DashFrame* dash_frame = dashframe_new_from_animframe(animframe);
                struct DashFramemap* dash_framemap = dashframemap_new_from_animframe(animframe);
                if( dash_frame && dash_framemap )
                {
                    dashmodel_animate(head_model, dash_frame, dash_framemap);
                    dashframe_free(dash_frame);
                    dashframemap_free(dash_framemap);
                }
            }
        }
    }

    /* Client.ts ignores component width/height for MODEL - model size comes from zoom/perspective.
     * Center at (x + width/2, y + height/2). Use fixed region size for chat heads. */
    int cx = x + (component->width / 2);
    int cy = y + (component->height / 2);
    int region_size = 128;
    int rx = cx - (region_size / 2);
    int ry = cy - (region_size / 2);

    int zoom = component->zoom;
    int xan = component->xan;
    int yan = component->yan;
    head_model_render_to_region(
        game, head_model, pixel_buffer, stride, rx, ry, region_size, region_size, zoom, xan, yan);
    dashmodel_free(head_model);
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
    // uint32_t encrypted_opcode = (opcode + isaac_next(game->random_out)) & 0xff;
    // game->outbound_buffer[game->outbound_size++] = encrypted_opcode;

    // // Send payload: p2(obj_id) + p2(slot) + p2(component_id)
    // // Based on Client.ts lines 9051-9053
    // game->outbound_buffer[game->outbound_size++] = (obj_id >> 8) & 0xFF;
    // game->outbound_buffer[game->outbound_size++] = obj_id & 0xFF;
    // game->outbound_buffer[game->outbound_size++] = (slot >> 8) & 0xFF;
    // game->outbound_buffer[game->outbound_size++] = slot & 0xFF;
    // game->outbound_buffer[game->outbound_size++] = (component_id >> 8) & 0xFF;
    // game->outbound_buffer[game->outbound_size++] = component_id & 0xFF;

    // Update selection state (Client.ts lines 9055-9066)
    // game->selected_cycle = 0;
    // game->selected_interface = component_id;
    // game->selected_item = slot;
    // game->selected_area = 2; // Sidebar area

    // Check if this component belongs to viewport or chat (would change area)
    // For now we assume it's in sidebar (area 2)
    // TODO: Check component->layer against game->viewport_interface_id and game->chat_interface_id
}
