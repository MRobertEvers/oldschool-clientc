#include "interface.h"

#include "graphics/dash.h"
#include "obj_icon.h"
#include "osrs/buildcachedat.h"
#include "osrs/clientscript_vm.h"
#include "osrs/gamenet_send.h"
#include "osrs/ginput.h"
#include "osrs/dash_utils.h"
#include "osrs/entity_scenebuild.h"
#include "osrs/interface_state.h"
#include "osrs/minimenu_action.h"
#include "osrs/minimenu_game.h"
#include "osrs/player_stats.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/config_obj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INV_MENU_MAX 24

static struct DashPixFont*
interface_font_from_reftable(
    struct GGame* game,
    char const* name)
{
    if( !game || !game->ui_scene || !game->gamecache || !name )
        return NULL;
    int sid = gamecache_get_font_ref_id(game->gamecache, name);
    if( sid < 0 )
        return NULL;
    return uiscene_font_get(game->ui_scene, sid);
}

static struct DashSprite*
interface_component_sprite_from_reftable(
    struct GGame* game,
    char const* graphic_name)
{
    if( !game || !game->ui_scene || !game->gamecache || !graphic_name || !graphic_name[0] )
        return NULL;
    int eid = gamecache_get_component_sprite_element_id(game->gamecache, graphic_name);
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
    struct GameCacheComponent* component,
    int script_id)
{
    if( !game || !game->clientscript_vm )
        return -2;
    return clientscript_vm_if_var(game->clientscript_vm, game, component, script_id);
}

bool
interface_get_if_active(
    struct GGame* game,
    struct GameCacheComponent* component)
{
    if( !game || !game->clientscript_vm )
        return false;
    return clientscript_vm_if_active(game->clientscript_vm, game, component);
}

void
interface_expand_if_text_placeholders(
    struct GGame* game,
    struct GameCacheComponent* component,
    const char* src,
    char* out,
    int out_cap)
{
    if( !out || out_cap <= 0 )
        return;
    if( !src )
    {
        out[0] = '\0';
        return;
    }
    if( !game || !component || !strchr(src, '%') )
    {
        snprintf(out, (size_t)out_cap, "%s", src);
        return;
    }

    int cap = out_cap - 1;
    int exp_len = 0;
    for( int i = 0; src[i] != '\0' && exp_len < cap; )
    {
        if( src[i] == '%' && src[i + 1] != '\0' )
        {
            int script_idx = src[i + 1] - '1';
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
                for( int j = 0; j < n && val_buf[j] && exp_len < cap; j++ )
                    out[exp_len++] = val_buf[j];
                i += 2;
                continue;
            }
        }
        out[exp_len++] = src[i++];
    }
    out[exp_len] = '\0';
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
    struct GameCacheComponent* child,
    int obj_id,
    int slot)
{
    struct GameCacheObj* obj = gamecache_get_obj(game->gamecache, obj_id);
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
    struct GameCacheComponent* component,
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
        case COMPONENT_TYPE_INV_TEXT:
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

        struct GameCacheComponent* child =
            gamecache_get_component(game->gamecache, child_id);

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
        case COMPONENT_TYPE_INV_TEXT:
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
    struct GameCacheComponent* component,
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
    struct GameCacheComponent* component,
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

        struct GameCacheComponent* child =
            gamecache_get_component(game->gamecache, child_id);
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
    struct GameCacheComponent* root,
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

void
interface_update_region_hover_ids(struct GGame* game)
{
    if( !game || !game->iface )
        return;
    struct InterfaceState* iface = game->iface;
    iface->over_main_com_id = -1;
    iface->over_side_com_id = -1;
    iface->over_chat_com_id = -1;
    iface->current_hovered_interface_id = -1;

    if( !game->gamecache )
        return;

    int mx = game->mouse.cursor_x;
    int my = game->mouse.cursor_y;

    /* Region rectangles match Client.ts menu hit-testing (main / sidebar / chat). */
    if( mx > 4 && my > 4 && mx < 516 && my < 338 && iface->viewport_interface_id >= 0 )
    {
        struct GameCacheComponent* root =
            gamecache_get_component(game->gamecache, iface->viewport_interface_id);
        iface->over_main_com_id = interface_find_hovered_interface_id(game, root, 4, 4, mx, my);
    }

    if( iface_point_in_sidebar_overlay(mx, my) )
    {
        if( iface->sidebar_interface_id >= 0 )
        {
            struct GameCacheComponent* root =
                gamecache_get_component(game->gamecache, iface->sidebar_interface_id);
            iface->over_side_com_id = interface_find_hovered_interface_id(
                game, root, IFACE_SIDEBAR_X0, IFACE_SIDEBAR_Y0, mx, my);
        }
        else if( game->ui_root_buffer )
        {
            /* Revconfig tab sidebar (expand_sidebar_rs_tree): root at bx,by like uitree_load. */
            for( uint32_t i = 0; i < game->ui_root_buffer->component_count; i++ )
            {
                struct StaticUIComponent* sb = &game->ui_root_buffer->components[i];
                if( sb->type != UIELEM_BUILTIN_SIDEBAR ||
                    sb->u.sidebar.tabno != iface->selected_tab )
                    continue;
                int compno = sb->u.sidebar.componentno;
                if( compno < 0 )
                    continue;
                struct GameCacheComponent* root =
                    gamecache_get_component(game->gamecache, compno);
                if( !root )
                    continue;
                int bx = sb->position.x + root->x;
                int by = sb->position.y + root->y;
                iface->over_side_com_id =
                    interface_find_hovered_interface_id(game, root, bx, by, mx, my);
                break;
            }
        }
    }

    if( mx > 17 && my > 357 && mx < 426 && my < 453 && iface->chat_interface_id >= 0 )
    {
        struct GameCacheComponent* root =
            gamecache_get_component(game->gamecache, iface->chat_interface_id);
        iface->over_chat_com_id = interface_find_hovered_interface_id(game, root, 17, 357, mx, my);
    }

    /* Aggregate: first region that has a hover wins (main > side > chat). */
    if( iface->over_main_com_id >= 0 )
        iface->current_hovered_interface_id = iface->over_main_com_id;
    else if( iface->over_side_com_id >= 0 )
        iface->current_hovered_interface_id = iface->over_side_com_id;
    else
        iface->current_hovered_interface_id = iface->over_chat_com_id;
}

static int
interface_inv_slot_index_at_mouse(
    struct GameCacheComponent* inv,
    int base_x,
    int base_y,
    int mx,
    int my)
{
    if( !inv || (inv->type != COMPONENT_TYPE_INV && inv->type != COMPONENT_TYPE_INV_TEXT) )
        return -1;
    int slot = 0;
    for( int row = 0; row < inv->height; row++ )
    {
        for( int col = 0; col < inv->width; col++ )
        {
            int slotX = base_x + col * (inv->marginX + 32);
            int slotY = base_y + row * (inv->marginY + 32);
            if( slot < 20 && inv->invSlotOffsetX && inv->invSlotOffsetY )
            {
                slotX += inv->invSlotOffsetX[slot];
                slotY += inv->invSlotOffsetY[slot];
            }
            if( mx >= slotX && mx < slotX + 32 && my >= slotY && my < slotY + 32 )
                return slot;
            slot++;
        }
    }
    return -1;
}

static bool
interface_cache_inv_slot_has_menu(
    struct GameCacheComponent* inv,
    int slot)
{
    int obj_id = 0;
    int nslots = inv->width * inv->height;
    if( slot >= 0 && slot < nslots && inv->invSlotObjId && slot < UI_INVENTORY_MAX_ITEMS )
        obj_id = inv->invSlotObjId[slot] > 0 ? inv->invSlotObjId[slot] - 1 : 0;
    if( obj_id > 0 )
        return true;
    if( inv->iop )
    {
        for( int i = 0; i < 5; i++ )
        {
            if( inv->iop[i] && inv->iop[i][0] )
                return true;
        }
    }
    return false;
}

/** Client.ts assigns hoveredSlot from cell geometry even when the slot is empty; menu picks
 * filter empty slots via interface_cache_inv_slot_has_menu — drag-drop needs geometry only. */
static bool
interface_sidebar_overlay_pick_inv_recursive_impl(
    struct GGame* game,
    struct GameCacheComponent* component,
    int abs_x,
    int abs_y,
    int scroll_y,
    int mx,
    int my,
    struct GameCacheComponent** out_inv,
    int* out_slot,
    bool require_menu_on_slot)
{
    if( !game || !game->iface || !game->gamecache )
        return false;

    for( int i = component->children_count - 1; i >= 0; i-- )
    {
        int child_id = component->children[i];
        struct GameCacheComponent* child =
            gamecache_get_component(game->gamecache, child_id);
        if( !child )
            continue;

        int childX = component->childX[i] + abs_x;
        int childY = component->childY[i] + abs_y - scroll_y;
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
            if( interface_sidebar_overlay_pick_inv_recursive_impl(
                    game,
                    child,
                    childX,
                    childY,
                    scroll_pos,
                    mx,
                    my,
                    out_inv,
                    out_slot,
                    require_menu_on_slot) )
                return true;
        }
        else if( child->type == COMPONENT_TYPE_INV || child->type == COMPONENT_TYPE_INV_TEXT )
        {
            int slot = interface_inv_slot_index_at_mouse(child, childX, childY, mx, my);
            if( slot >= 0 &&
                (!require_menu_on_slot || interface_cache_inv_slot_has_menu(child, slot)) )
            {
                *out_inv = child;
                *out_slot = slot;
                return true;
            }
        }
    }
    return false;
}

static bool
interface_sidebar_overlay_pick_inv_recursive(
    struct GGame* game,
    struct GameCacheComponent* component,
    int abs_x,
    int abs_y,
    int scroll_y,
    int mx,
    int my,
    struct GameCacheComponent** out_inv,
    int* out_slot)
{
    return interface_sidebar_overlay_pick_inv_recursive_impl(
        game, component, abs_x, abs_y, scroll_y, mx, my, out_inv, out_slot, true);
}

static bool
interface_sidebar_overlay_pick_inv_recursive_geom(
    struct GGame* game,
    struct GameCacheComponent* component,
    int abs_x,
    int abs_y,
    int scroll_y,
    int mx,
    int my,
    struct GameCacheComponent** out_inv,
    int* out_slot)
{
    return interface_sidebar_overlay_pick_inv_recursive_impl(
        game, component, abs_x, abs_y, scroll_y, mx, my, out_inv, out_slot, false);
}

/** Hit-test TYPE_INV / TYPE_INV_TEXT under (mx,my) for a layer root at (root_x, root_y).
 * Same geometry as interface_sidebar_overlay_pick_inv_recursive (Client.ts regions). */
static bool
interface_cache_pick_inv_in_layer_root_geom(
    struct GGame* game,
    struct GameCacheComponent* root,
    int root_x,
    int root_y,
    int mx,
    int my,
    struct GameCacheComponent** out_inv,
    int* out_slot)
{
    if( !root || root->type != COMPONENT_TYPE_LAYER || !out_inv || !out_slot )
        return false;
    return interface_sidebar_overlay_pick_inv_recursive_geom(
        game, root, root_x, root_y, 0, mx, my, out_inv, out_slot);
}

/** Cache-tree inv under cursor: main viewport (4,4), sidebar overlay, tab sidebar, chat. */
static bool
interface_cache_pick_inv_at_screen_geom(
    struct GGame* game,
    int mx,
    int my,
    struct GameCacheComponent** out_inv,
    int* out_slot)
{
    struct InterfaceState* iface = game ? game->iface : NULL;
    if( !game || !iface || !game->gamecache || !out_inv || !out_slot )
        return false;
    *out_inv = NULL;
    *out_slot = -1;

    if( mx > 4 && my > 4 && mx < 516 && my < 338 && iface->viewport_interface_id >= 0 )
    {
        struct GameCacheComponent* root =
            gamecache_get_component(game->gamecache, iface->viewport_interface_id);
        if( interface_cache_pick_inv_in_layer_root_geom(
                game, root, 4, 4, mx, my, out_inv, out_slot) )
            return true;
    }

    if( iface_point_in_sidebar_overlay(mx, my) )
    {
        if( iface->sidebar_interface_id >= 0 )
        {
            struct GameCacheComponent* root =
                gamecache_get_component(game->gamecache, iface->sidebar_interface_id);
            if( interface_cache_pick_inv_in_layer_root_geom(
                    game, root, IFACE_SIDEBAR_X0, IFACE_SIDEBAR_Y0, mx, my, out_inv, out_slot) )
                return true;
        }
        else if( game->ui_root_buffer )
        {
            for( uint32_t i = 0; i < game->ui_root_buffer->component_count; i++ )
            {
                struct StaticUIComponent* sb = &game->ui_root_buffer->components[i];
                if( sb->type != UIELEM_BUILTIN_SIDEBAR ||
                    sb->u.sidebar.tabno != iface->selected_tab )
                    continue;
                int compno = sb->u.sidebar.componentno;
                if( compno < 0 )
                    continue;
                struct GameCacheComponent* root =
                    gamecache_get_component(game->gamecache, compno);
                if( !root )
                    continue;
                int bx = sb->position.x + root->x;
                int by = sb->position.y + root->y;
                if( interface_cache_pick_inv_in_layer_root_geom(
                        game, root, bx, by, mx, my, out_inv, out_slot) )
                    return true;
                break;
            }
        }
    }

    if( mx > 17 && my > 357 && mx < 426 && my < 453 && iface->chat_interface_id >= 0 )
    {
        struct GameCacheComponent* root =
            gamecache_get_component(game->gamecache, iface->chat_interface_id);
        if( interface_cache_pick_inv_in_layer_root_geom(
                game, root, 17, 357, mx, my, out_inv, out_slot) )
            return true;
    }

    return false;
}

/** Hit-test TYPE_INV / TYPE_INV_TEXT under (mx,my) for a layer root at (root_x, root_y).
 * Same geometry as interface_sidebar_overlay_pick_inv_recursive (Client.ts regions). */
static bool
interface_cache_pick_inv_in_layer_root(
    struct GGame* game,
    struct GameCacheComponent* root,
    int root_x,
    int root_y,
    int mx,
    int my,
    struct GameCacheComponent** out_inv,
    int* out_slot)
{
    if( !root || root->type != COMPONENT_TYPE_LAYER || !out_inv || !out_slot )
        return false;
    return interface_sidebar_overlay_pick_inv_recursive(
        game, root, root_x, root_y, 0, mx, my, out_inv, out_slot);
}

/** Cache-tree inv under cursor: main viewport (4,4), sidebar overlay, tab sidebar, chat. */
static bool
interface_cache_pick_inv_at_screen(
    struct GGame* game,
    int mx,
    int my,
    struct GameCacheComponent** out_inv,
    int* out_slot)
{
    struct InterfaceState* iface = game ? game->iface : NULL;
    if( !game || !iface || !game->gamecache || !out_inv || !out_slot )
        return false;
    *out_inv = NULL;
    *out_slot = -1;

    if( mx > 4 && my > 4 && mx < 516 && my < 338 && iface->viewport_interface_id >= 0 )
    {
        struct GameCacheComponent* root =
            gamecache_get_component(game->gamecache, iface->viewport_interface_id);
        if( interface_cache_pick_inv_in_layer_root(game, root, 4, 4, mx, my, out_inv, out_slot) )
            return true;
    }

    if( iface_point_in_sidebar_overlay(mx, my) )
    {
        if( iface->sidebar_interface_id >= 0 )
        {
            struct GameCacheComponent* root =
                gamecache_get_component(game->gamecache, iface->sidebar_interface_id);
            if( interface_cache_pick_inv_in_layer_root(
                    game, root, IFACE_SIDEBAR_X0, IFACE_SIDEBAR_Y0, mx, my, out_inv, out_slot) )
                return true;
        }
        else if( game->ui_root_buffer )
        {
            for( uint32_t i = 0; i < game->ui_root_buffer->component_count; i++ )
            {
                struct StaticUIComponent* sb = &game->ui_root_buffer->components[i];
                if( sb->type != UIELEM_BUILTIN_SIDEBAR ||
                    sb->u.sidebar.tabno != iface->selected_tab )
                    continue;
                int compno = sb->u.sidebar.componentno;
                if( compno < 0 )
                    continue;
                struct GameCacheComponent* root =
                    gamecache_get_component(game->gamecache, compno);
                if( !root )
                    continue;
                int bx = sb->position.x + root->x;
                int by = sb->position.y + root->y;
                if( interface_cache_pick_inv_in_layer_root(
                        game, root, bx, by, mx, my, out_inv, out_slot) )
                    return true;
                break;
            }
        }
    }

    if( mx > 17 && my > 357 && mx < 426 && my < 453 && iface->chat_interface_id >= 0 )
    {
        struct GameCacheComponent* root =
            gamecache_get_component(game->gamecache, iface->chat_interface_id);
        if( interface_cache_pick_inv_in_layer_root(game, root, 17, 357, mx, my, out_inv, out_slot) )
            return true;
    }

    return false;
}

/*
 * Which build-cache interface root covers the sidebar content area.
 *
 * `sidebar_interface_id` is only non-negative after IF_OPENSIDE / IF_OPENMAIN_SIDE (Client.ts
 * sideModalId). Normal tab UIs stay at -1: each tab's root arrives via IF_SETTAB into
 * `tab_interface_id[tab]`, and redstone clicks only change `selected_tab` (Client.ts tabLoop /
 * sideTab) — they do not assign the modal side id.
 *
 * This helper mirrors addComponentOptions: modal side if open, else the active tab's overlay.
 * Without the IF_SETTAB branch, hover/minimenu fallback that walks cache inv under the sidebar
 * would almost always bail when no IF_OPENSIDE is in effect.
 */
static int
interface_sidebar_overlay_root_component_id(struct InterfaceState const* iface)
{
    if( !iface )
        return -1;
    if( iface->sidebar_interface_id >= 0 )
        return iface->sidebar_interface_id;
    int tab = iface->selected_tab;
    if( tab >= 0 && tab < 14 )
        return iface->tab_interface_id[tab];
    return -1;
}

bool
interface_sidebar_overlay_try_fill_uitree_options(
    struct GGame* game,
    int pick_mx,
    int pick_my,
    struct UITreeOptionSet* os,
    int* out_inv_component_id,
    int* out_inv_slot)
{
    if( !game || !os || !game->iface || !game->gamecache )
        return false;

    int mx = pick_mx;
    int my = pick_my;
    if( !iface_point_in_sidebar_overlay(mx, my) )
        return false;

    int root_id = interface_sidebar_overlay_root_component_id(game->iface);
    if( root_id < 0 )
        return false;

    struct GameCacheComponent* root =
        gamecache_get_component(game->gamecache, root_id);
    if( !root || root->type != COMPONENT_TYPE_LAYER )
        return false;

    struct GameCacheComponent* hit_inv = NULL;
    int slot = -1;
    if( !interface_sidebar_overlay_pick_inv_recursive(
            game, root, IFACE_SIDEBAR_X0, IFACE_SIDEBAR_Y0, 0, mx, my, &hit_inv, &slot) ||
        !hit_inv || slot < 0 )
        return false;

    uitree_fill_inv_slot_options_from_cache_inv(game, hit_inv, slot, os);
    if( os->option_count > 0 )
    {
        if( out_inv_component_id )
            *out_inv_component_id = hit_inv->id;
        if( out_inv_slot )
            *out_inv_slot = slot;
        return true;
    }
    return false;
}

int
interface_hover_tooltip_line(
    struct GGame* game,
    char* out,
    int out_cap)
{
    if( !game || !game->iface || !game->gamecache || !out || out_cap <= 0 )
        return 0;
    int cid = game->iface->current_hovered_interface_id;
    if( cid < 0 )
        return 0;
    struct GameCacheComponent* c = gamecache_get_component(game->gamecache, cid);
    if( !c )
        return 0;

    char tmp[512];

    if( c->targetVerb && c->targetVerb[0] )
    {
        if( c->targetText && c->targetText[0] )
            snprintf(tmp, sizeof(tmp), "%s %s", c->targetVerb, c->targetText);
        else
            snprintf(tmp, sizeof(tmp), "%s", c->targetVerb);
        interface_expand_if_text_placeholders(game, c, tmp, out, out_cap);
        return 1;
    }
    if( c->option && c->option[0] )
    {
        interface_expand_if_text_placeholders(game, c, c->option, out, out_cap);
        return 1;
    }
    if( c->iop )
    {
        for( int i = 0; i < 5; i++ )
        {
            if( c->iop[i] && c->iop[i][0] )
            {
                interface_expand_if_text_placeholders(game, c, c->iop[i], out, out_cap);
                return 1;
            }
        }
    }
    if( c->type == COMPONENT_TYPE_TEXT && c->text && c->text[0] )
    {
        interface_expand_if_text_placeholders(game, c, c->text, out, out_cap);
        return 1;
    }
    if( c->buttonType == COMPONENT_BUTTON_TYPE_CLOSE )
    {
        snprintf(out, (size_t)out_cap, "Close");
        return 1;
    }
    if( c->buttonType == COMPONENT_BUTTON_TYPE_CONTINUE )
    {
        snprintf(out, (size_t)out_cap, "Continue");
        return 1;
    }
    return 0;
}

bool
interface_component_is_overlay_hovered(
    struct GGame* game,
    int component_id)
{
    if( !game || !game->iface || component_id < 0 )
        return false;
    struct InterfaceState* i = game->iface;
    return component_id == i->over_main_com_id || component_id == i->over_side_com_id ||
           component_id == i->over_chat_com_id;
}

/* Recursive hit-test for scrollbar; mirrors layer child loop in interface_draw_component.
 * Returns component id of scrollable layer whose scrollbar contains (mouse_x, mouse_y), or -1.
 * On hit, sets *out_scrollbar_y, *out_height, *out_scroll_height for that layer. */
static int
find_scrollbar_at_recursive(
    struct GGame* game,
    struct GameCacheComponent* component,
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

            struct GameCacheComponent* child =
                gamecache_get_component(game->gamecache, child_id);
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
    struct GameCacheComponent* root,
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
is_clickable_button(struct GameCacheComponent* child)
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
    struct GameCacheComponent* component,
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

        struct GameCacheComponent* child =
            gamecache_get_component(game->gamecache, child_id);
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
    struct GameCacheComponent* root,
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
    struct GameCacheComponent* com =
        gamecache_get_component(game->gamecache, component_id);
    if( !com )
        return;

    /* Client.ts doAction: scripts[0][0]==5 (pushvar) or [0]==14 (push_varbit).
     * Client only does optimistic update for opcode 5; we extend to 14 for varbit toggles. */
    if( !com->scripts || com->scripts_count < 1 || !com->scripts[0] )
        return;
    int* script = com->scripts[0];
    int opcode = script[0];

    struct ClientScriptVM* vm = game->clientscript_vm;

    if( opcode == 5 )
    {
        /* pushvar {id} */
        int varp = script[1];
        int current = clientscript_vm_get_var(vm, varp);

        if( com->buttonType == COMPONENT_BUTTON_TYPE_TOGGLE )
        {
            clientscript_vm_set_var_optimistic(vm, varp, 1 - current);
        }
        else if( com->buttonType == COMPONENT_BUTTON_TYPE_SELECT && com->scriptOperand )
        {
            int operand = com->scriptOperand[0];
            if( current != operand )
                clientscript_vm_set_var_optimistic(vm, varp, operand);
        }
    }
    else if( opcode == 14 && com->buttonType == COMPONENT_BUTTON_TYPE_TOGGLE )
    {
        /* push_varbit {varbit} - toggle the varbit's bits in the base varp */
        int varbit_id = script[1];
        if( !vm || varbit_id < 0 || varbit_id >= vm->varbit_count )
            return;
        int current = clientscript_vm_get_varbit(vm, varbit_id);
        int new_val = 1 - current;

        const struct VarBitType* vb = &vm->varbit_types[varbit_id];
        if( vb->basevar < 0 || vb->basevar >= vm->varp_count )
            return;

        int bit_count = vb->endbit - vb->startbit;
        if( bit_count <= 0 || bit_count >= VARP_VARBIT_READBIT_MAX )
            return;

        int mask = vm->readbit[bit_count];
        int base_val = clientscript_vm_get_var(vm, vb->basevar);
        int cleared = base_val & ~(mask << vb->startbit);
        int updated = cleared | ((new_val & mask) << vb->startbit);
        clientscript_vm_set_var_optimistic(vm, vb->basevar, updated);
    }
    else if( opcode == 14 && com->buttonType == COMPONENT_BUTTON_TYPE_SELECT && com->scriptOperand )
    {
        /* push_varbit + SELECT: set varbit to operand */
        int varbit_id = script[1];
        if( !vm || varbit_id < 0 || varbit_id >= vm->varbit_count )
            return;
        int operand = com->scriptOperand[0];
        int current = clientscript_vm_get_varbit(vm, varbit_id);
        if( current == operand )
            return;

        const struct VarBitType* vb = &vm->varbit_types[varbit_id];
        if( vb->basevar < 0 || vb->basevar >= vm->varp_count )
            return;

        int bit_count = vb->endbit - vb->startbit;
        if( bit_count <= 0 || bit_count >= VARP_VARBIT_READBIT_MAX )
            return;

        int mask = vm->readbit[bit_count];
        int base_val = clientscript_vm_get_var(vm, vb->basevar);
        int cleared = base_val & ~(mask << vb->startbit);
        int updated = cleared | ((operand & mask) << vb->startbit);
        clientscript_vm_set_var_optimistic(vm, vb->basevar, updated);
    }
}

void
interface_dispatch_button_action(struct GGame* game, int component_id, int btn_action)
{
    if( !game )
        return;
    switch( btn_action )
    {
    case IF_BUTTON_ACTION_CLOSE_MODAL:
        gamenet_send_close_modal(game);
        break;
    case IF_BUTTON_ACTION_RESUME_PAUSEBUTTON:
        gamenet_send_resume_pausebutton(game, component_id);
        break;
    default:
        gamenet_send_if_button(game, component_id);
        break;
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

/* Mirrors Client.ts doScrollbar track/grip drag (lines 10543-10558).
 * Computes scrollPos so the grip thumb is centred on mouse_y.
 * gripSize = max(16, height*height/scroll_height) (same formula as drawScrollbar).
 * gripY    = mouse_y - layer_y - gripSize/2 - 16  (centred; top 16px is up-arrow).
 * maxY     = height - gripSize - 32                (track length).
 * scrollPos = (max_scroll * gripY) / maxY, then clamped to [0, max_scroll]. */
void
interface_handle_scrollbar_drag(
    struct GGame* game,
    int component_id,
    int layer_y,
    int height,
    int scroll_height,
    int mouse_y)
{
    if( !game->iface )
        return;
    if( component_id < 0 || component_id >= MAX_IFACE_SCROLL_IDS )
        return;
    int max_scroll = scroll_height - height;
    if( max_scroll <= 0 )
        return;
    int track_h = height - 32;
    if( track_h <= 0 )
        return;
    int grip_size = (track_h * height) / scroll_height;
    if( grip_size < 8 )
        grip_size = 8;
    if( grip_size > track_h )
        grip_size = track_h;
    int max_y = track_h - grip_size;
    if( max_y <= 0 )
        return;
    int grip_y = mouse_y - layer_y - 16;
    if( grip_y < 0 )
        grip_y = 0;
    if( grip_y > max_y )
        grip_y = max_y;
    int new_pos = (int)((long)max_scroll * (long)grip_y / (long)max_y);
    if( new_pos < 0 )
        new_pos = 0;
    if( new_pos > max_scroll )
        new_pos = max_scroll;
    game->iface->component_scroll_position[component_id] = new_pos;
}

void
interface_draw_component_rect(
    struct GGame* game,
    struct GameCacheComponent* component,
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
    bool hovered = interface_component_is_overlay_hovered(game, component->id);
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
                vp, pixel_buffer, stride, x, y, component->width, component->height, colour, alpha);
        else
            draw_rect_clipped(
                vp, pixel_buffer, stride, x, y, component->width, component->height, colour);
    }
}

void
interface_draw_component_text(
    struct GGame* game,
    struct GameCacheComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride)
{
    if( !game->iface || !game->gamecache )
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
    bool hovered = interface_component_is_overlay_hovered(game, component->id);
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
            interface_expand_if_text_placeholders(
                game, component, line_buf, expanded_buf, (int)sizeof(expanded_buf));

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

    /* Draw track (y+16, track_h) clipped to viewport — Client.ts SCROLLBAR_TRACK */
    int track_y = y + 16;
    int draw_y0 = track_y < ct ? ct : track_y;
    int draw_y1 = track_y + track_h;
    if( draw_y1 > cb )
        draw_y1 = cb;
    for( int py = draw_y0; py < draw_y1; py++ )
        for( int px = x; px < x + 16 && px < cr; px++ )
            if( px >= cl )
                pixel_buffer[py * stride + px] = 0x23201b;

    /* Grip fill — Client.ts SCROLLBAR_GRIP_FOREGROUND */
    int grip_y0 = y + 16 + grip_y;
    int grip_y1 = grip_y0 + grip_size;
    draw_y0 = grip_y0 < ct ? ct : grip_y0;
    draw_y1 = grip_y1 > cb ? cb : grip_y1;
    for( int py = draw_y0; py < draw_y1; py++ )
        for( int px = x; px < x + 16 && px < cr; px++ )
            if( px >= cl )
                pixel_buffer[py * stride + px] = 0x4d4233;

    /* Bevels on top — Client.ts vline/hline after grip fill */
    for( int py = grip_y0; py < grip_y0 + grip_size; py++ )
    {
        if( py < ct || py >= cb )
            continue;
        if( x >= cl && x < cr )
            pixel_buffer[py * stride + x] = 0x766654;
        if( x + 1 >= cl && x + 1 < cr )
            pixel_buffer[py * stride + (x + 1)] = 0x766654;
        if( x + 15 >= cl && x + 15 < cr )
            pixel_buffer[py * stride + (x + 15)] = 0x332d25;
    }
    if( grip_size > 1 )
    {
        for( int py = grip_y0 + 1; py < grip_y0 + grip_size; py++ )
        {
            if( py < ct || py >= cb )
                continue;
            if( x + 14 >= cl && x + 14 < cr )
                pixel_buffer[py * stride + (x + 14)] = 0x332d25;
        }
    }
    for( int px = x; px < x + 16 && px < cr; px++ )
    {
        if( px < cl )
            continue;
        if( grip_y0 >= ct && grip_y0 < cb )
            pixel_buffer[grip_y0 * stride + px] = 0x766654;
        if( grip_y0 + 1 >= ct && grip_y0 + 1 < cb )
            pixel_buffer[(grip_y0 + 1) * stride + px] = 0x766654;
    }
    {
        int yb0 = grip_y0 + grip_size - 1;
        if( yb0 >= ct && yb0 < cb )
        {
            for( int px = x; px < x + 16 && px < cr; px++ )
            {
                if( px >= cl )
                    pixel_buffer[yb0 * stride + px] = 0x332d25;
            }
        }
        if( grip_size > 1 )
        {
            int yb1 = grip_y0 + grip_size - 2;
            if( yb1 >= ct && yb1 < cb )
            {
                for( int px = x + 1; px < x + 16 && px < cr; px++ )
                {
                    if( px >= cl )
                        pixel_buffer[yb1 * stride + px] = 0x332d25;
                }
            }
        }
    }
}

void
interface_draw_component_graphic(
    struct GGame* game,
    struct GameCacheComponent* component,
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
    struct GameCacheComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride)
{
    if( !game->iface || !game->gamecache )
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
                    struct InterfaceState* iface = game->iface;
                    bool is_drag_slot =
                        (iface->inv_drag_area != 0 && iface->inv_drag_comp_id == component->id &&
                         iface->inv_drag_slot == slot);
                    bool is_selected =
                        (iface->selected_area != 0 && iface->selected_item == slot &&
                         iface->selected_interface == component->id);

                    int dx = 0;
                    int dy = 0;
                    if( is_drag_slot )
                        interface_inv_drag_delta(game, &dx, &dy);

                    if( is_drag_slot )
                    {
                        dash2d_blit_sprite_alpha(
                            game->sys_dash,
                            icon,
                            game->iface_view_port,
                            slotX + dx,
                            slotY + dy,
                            128,
                            pixel_buffer);
                    }
                    else if( is_selected )
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
                            slotX + dx + 1,
                            slotY + 10 + dy,
                            0x000000,
                            pixel_buffer,
                            stride);
                        dashfont_draw_text(
                            p11,
                            (uint8_t*)count_str,
                            slotX + dx,
                            slotY + 9 + dy,
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
    struct GameCacheComponent* component,
    int x,
    int y,
    int* pixel_buffer,
    int stride)
{
    if( !game || !game->clientscript_vm || !pixel_buffer )
        return;

    bool active = clientscript_vm_if_active(game->clientscript_vm, game, component);
    int mt = component->modelType;
    int mid = component->model;
    if( active && component->activeModelType != 0 )
    {
        mt = component->activeModelType;
        mid = component->activeModel;
    }
    if( mt != 2 && mt != 3 )
        return;

    int* slots = NULL;
    int* colors = NULL;
    if( mt == 3 )
    {
        struct PlayerEntity* local = world_player(game->world, ACTIVE_PLAYER_SLOT);
        if( !local->alive )
            return;
        slots = local->appearance.slots;
        colors = local->appearance.colors;
    }

    struct DashModel* head_model = entity_scenebuild_head_model_for_component_lights(
        game, mt, mid, slots, colors, false);
    if( !head_model )
        return;

    int sequence_id =
        uitree_interface_resolved_sequence_id(game, component, active, mt, mid);
    if( sequence_id >= 0 && game->gamecache && game->gamecache->sequences_hmap )
    {
        struct GameCacheSequence* seq = gamecache_get_sequence(game->gamecache, sequence_id);
        if( seq && seq->frame_count > 0 && seq->frames )
        {
            int frame_i = (game->cycle / 5) % seq->frame_count;
            uitree_interface_apply_sequence_keyframe_to_model(game, head_model, seq, frame_i);
        }
    }

    entity_scenebuild_interface_head_apply_default_light(head_model);

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
    struct GameCacheComponent* component,
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

static bool
inv_action_eligible_for_drag_handoff(int action)
{
    if( action == (int)MINIMENU_ACTION_OPHELDT_START )
        return true;
    int base = action;
    if( base > (int)MINIMENU_ACTION_PRIORITY_OFFSET )
        base -= (int)MINIMENU_ACTION_PRIORITY_OFFSET;
    if( base == (int)MINIMENU_ACTION_OPHELD6 )
        return true;
    if( base == (int)MINIMENU_ACTION_INV_BUTTON1 || base == (int)MINIMENU_ACTION_INV_BUTTON2 ||
        base == (int)MINIMENU_ACTION_INV_BUTTON3 || base == (int)MINIMENU_ACTION_INV_BUTTON4 ||
        base == (int)MINIMENU_ACTION_INV_BUTTON5 )
        return true;
    if( base == (int)MINIMENU_ACTION_OPHELD1 || base == (int)MINIMENU_ACTION_OPHELD2 ||
        base == (int)MINIMENU_ACTION_OPHELD3 || base == (int)MINIMENU_ACTION_OPHELD4 ||
        base == (int)MINIMENU_ACTION_OPHELD5 )
        return true;
    return false;
}

static int
interface_inv_drag_area_from_xy(struct GGame* game, int mx, int my)
{
    (void)game;
    if( iface_point_in_sidebar_overlay(mx, my) )
        return 2;
    if( mx > 4 && my > 4 && mx < 516 && my < 338 )
        return 1;
    if( mx >= 4 && my >= 357 && mx < 516 && my < 503 )
        return 3;
    return 2;
}

/** Buffer-space point for drag threshold, sprite delta, and drop: cursor, release on mouse-up,
 * last-good while cursor was inside the game rect, else grab. */
static void
interface_inv_drag_pick_xy(struct GGame* game, int* out_x, int* out_y)
{
    struct InterfaceState* iface = game ? game->iface : NULL;
    if( !out_x || !out_y )
        return;
    if( !iface || iface->inv_drag_area == 0 )
    {
        *out_x = 0;
        *out_y = 0;
        return;
    }

    if( game->mouse.cursor_x >= 0 && game->mouse.cursor_y >= 0 )
    {
        *out_x = game->mouse.cursor_x;
        *out_y = game->mouse.cursor_y;
        return;
    }

    if( game->mouse.buttons[TORIRSM_LEFT].release_this_frame &&
        game->mouse.buttons[TORIRSM_LEFT].release_x >= 0 &&
        game->mouse.buttons[TORIRSM_LEFT].release_y >= 0 )
    {
        *out_x = game->mouse.buttons[TORIRSM_LEFT].release_x;
        *out_y = game->mouse.buttons[TORIRSM_LEFT].release_y;
        return;
    }

    if( iface->inv_drag_last_good_x >= 0 && iface->inv_drag_last_good_y >= 0 )
    {
        *out_x = iface->inv_drag_last_good_x;
        *out_y = iface->inv_drag_last_good_y;
        return;
    }

    *out_x = iface->inv_drag_grab_x;
    *out_y = iface->inv_drag_grab_y;
}

void
interface_inv_drag_delta(struct GGame* game, int* out_dx, int* out_dy)
{
    if( !out_dx || !out_dy )
        return;
    *out_dx = 0;
    *out_dy = 0;
    if( !game || !game->iface || game->iface->inv_drag_area == 0 )
        return;

    int px, py;
    interface_inv_drag_pick_xy(game, &px, &py);
    int dx = px - game->iface->inv_drag_grab_x;
    int dy = py - game->iface->inv_drag_grab_y;
    if( dx < 5 && dx > -5 )
        dx = 0;
    if( dy < 5 && dy > -5 )
        dy = 0;
    if( game->iface->inv_drag_cycles < 5 )
    {
        dx = 0;
        dy = 0;
    }
    *out_dx = dx;
    *out_dy = dy;
}

void
interface_inv_try_drag_mouse_down(struct GGame* game, int mx, int my)
{
    struct UITreeScrollbarHit sb;

    if( !game || !game->iface || !game->gamecache )
        return;

    if( game->ui_root_buffer && uitree_find_scrollbar_at(game, mx, my, &sb) )
        return;

    if( game->ui_root_buffer )
        uitree_sync_hover_option_set(game, mx, my);

    int hc = -1;
    int hs = -1;
    if( game->ui_root_buffer )
    {
        hc = game->ui_root_buffer->hover_inv_component_id;
        hs = game->ui_root_buffer->hover_inv_slot;
    }

    struct GameCacheComponent* cc = NULL;
    if( hc >= 0 && hs >= 0 )
    {
        cc = gamecache_get_component(game->gamecache, hc);
        if( cc && cc->type != COMPONENT_TYPE_INV && cc->type != COMPONENT_TYPE_INV_TEXT )
            cc = NULL;
    }

    if( !cc )
    {
        struct GameCacheComponent* picked = NULL;
        int picked_slot = -1;
        if( !interface_cache_pick_inv_at_screen(game, mx, my, &picked, &picked_slot) || !picked ||
            picked_slot < 0 )
            return;
        cc = picked;
        hc = picked->id;
        hs = picked_slot;
    }

    if( !cc || hs < 0 )
        return;

    int wire = (cc->invSlotObjId && hs >= 0 && hs < cc->width * cc->height) ? cc->invSlotObjId[hs]
                                                                              : 0;
    int obj_def = wire > 0 ? wire - 1 : 0;

    int def_act = interface_get_inv_default_action(game, cc, obj_def, hs);
    if( !inv_action_eligible_for_drag_handoff(def_act) )
        return;
    if( !cc->swappable && !cc->draggable )
        return;

    struct InterfaceState* iface       = game->iface;
    iface->inv_drag_area               = interface_inv_drag_area_from_xy(game, mx, my);
    iface->inv_drag_comp_id            = hc;
    iface->inv_drag_slot               = hs;
    iface->inv_drag_cycles             = 0;
    iface->inv_drag_grab_threshold     = 0;
    iface->inv_drag_grab_x             = mx;
    iface->inv_drag_grab_y             = my;
    iface->inv_drag_last_good_x        = mx;
    iface->inv_drag_last_good_y        = my;
}

void
interface_inv_drag_tick(struct GGame* game)
{
    struct InterfaceState* iface = game ? game->iface : NULL;
    if( !iface || iface->inv_drag_area == 0 )
        return;

    iface->inv_drag_cycles++;
    if( game->mouse.cursor_x >= 0 && game->mouse.cursor_y >= 0 )
    {
        iface->inv_drag_last_good_x = game->mouse.cursor_x;
        iface->inv_drag_last_good_y = game->mouse.cursor_y;
    }

    int gx = iface->inv_drag_grab_x;
    int gy = iface->inv_drag_grab_y;
    int mx, my;
    interface_inv_drag_pick_xy(game, &mx, &my);
    if( mx > gx + 5 || mx < gx - 5 || my > gy + 5 || my < gy - 5 )
        iface->inv_drag_grab_threshold = 1;
}

/**
 * Client.ts inventory drag-drop prediction: swap or replace slots locally, mirror TS `objReplace`
 * / `swapSlots`, then server confirms via UPDATE_INV_*.
 * Bank insert (mode 1 + CC_BANKMODE 206) is not wired yet — always send mode 0 except forcing 0
 * when destination slot is empty (Client.ts).
 */
static void
interface_inv_client_predict_drag(
    struct GGame* game,
    int comp_id,
    int from_slot,
    int to_slot,
    int* out_mode)
{
    if( !out_mode )
        return;
    *out_mode = 0;

    if( !game || !game->gamecache )
        return;

    struct GameCacheComponent* cc = gamecache_get_component(game->gamecache, comp_id);
    if( !cc || !cc->invSlotObjId || !cc->invSlotObjCount )
        return;

    int nslots = cc->width * cc->height;
    if( nslots <= 0 || from_slot < 0 || to_slot < 0 || from_slot >= nslots || to_slot >= nslots )
        return;

    /* Client.ts: dst empty forces mode 0 (overrides bank insert mode). */
    if( cc->invSlotObjId[to_slot] <= 0 )
        *out_mode = 0;

    if( cc->draggable )
    {
        cc->invSlotObjId[to_slot]     = cc->invSlotObjId[from_slot];
        cc->invSlotObjCount[to_slot] = cc->invSlotObjCount[from_slot];
        cc->invSlotObjId[from_slot]     = 0;
        cc->invSlotObjCount[from_slot] = 0;
    }
    else
    {
        int tmp_id  = cc->invSlotObjId[from_slot];
        int tmp_cnt = cc->invSlotObjCount[from_slot];
        cc->invSlotObjId[from_slot]     = cc->invSlotObjId[to_slot];
        cc->invSlotObjCount[from_slot]  = cc->invSlotObjCount[to_slot];
        cc->invSlotObjId[to_slot]       = tmp_id;
        cc->invSlotObjCount[to_slot]    = tmp_cnt;
    }

    if( !game->inv_pool || !game->ui_root_buffer || from_slot >= UI_INVENTORY_MAX_ITEMS ||
        to_slot >= UI_INVENTORY_MAX_ITEMS )
        return;

    int32_t node_idx = -1;
    int inv_index =
        uitree_find_inv_index_by_component_id(game->ui_root_buffer, comp_id, &node_idx);
    if( inv_index < 0 || inv_index >= game->inv_pool->count )
        return;

    struct UIInventory* inv       = &game->inv_pool->inventories[inv_index];
    struct UIInventoryItem* it_a  = &inv->items[from_slot];
    struct UIInventoryItem* it_b  = &inv->items[to_slot];

    if( cc->draggable )
    {
        if( it_b->scene_id >= 0 && game->ui_scene )
            uiscene_element_release(game->ui_scene, it_b->scene_id);
        *it_b = *it_a;
        it_a->obj_id      = 0;
        it_a->obj_count   = 0;
        it_a->scene_id    = -1;
        it_a->atlas_index = 0;
    }
    else
    {
        struct UIInventoryItem tmp = *it_a;
        *it_a                         = *it_b;
        *it_b                         = tmp;
    }

    if( node_idx >= 0 && (uint32_t)node_idx < game->ui_root_buffer->component_count )
        game->ui_root_buffer->components[node_idx].is_dirty = 1;
}

void
interface_inv_try_drag_mouse_up(struct GGame* game, struct GInput* input)
{
    struct InterfaceState* iface = game ? game->iface : NULL;
    if( !iface || !input || iface->inv_drag_area == 0 )
        return;

    int grab_x    = iface->inv_drag_grab_x;
    int grab_y    = iface->inv_drag_grab_y;
    int comp      = iface->inv_drag_comp_id;
    int from_slot = iface->inv_drag_slot;
    int th        = iface->inv_drag_grab_threshold;
    int cycles    = iface->inv_drag_cycles;

    int pick_x;
    int pick_y;
    interface_inv_drag_pick_xy(game, &pick_x, &pick_y);

    iface->inv_drag_area           = 0;
    iface->inv_drag_cycles         = 0;
    iface->inv_drag_grab_threshold = 0;
    iface->inv_drag_last_good_x    = -1;
    iface->inv_drag_last_good_y    = -1;

    {
        char const* dbg = getenv("TORI_DEBUG_INV_DRAG");
        if( dbg && dbg[0] && strcmp(dbg, "0") != 0 )
            fprintf(
                stderr,
                "[TORI_DEBUG_INV_DRAG] mouse_up: th=%d cycles=%d net_state=%d "
                "comp=%d from_slot=%d grab=(%d,%d) pick=(%d,%d)\n",
                th,
                cycles,
                (int)game->net_state,
                comp,
                from_slot,
                grab_x,
                grab_y,
                pick_x,
                pick_y);
    }

    /* Gesture qualified as a drag (moved far enough, held long enough).
     * GAME_NET_STATE_GAME mirrors Client.ts implicit assumption: INV_BUTTOND is
     * only meaningful while connected.  If not in-game we absorb the release
     * silently rather than falling through to fire the primary item action (which
     * would be wrong — the user was visually dragging, not clicking). */
    if( th && cycles >= 5 )
    {
        if( GAME_NET_STATE_GAME != game->net_state )
            return;

        uitree_sync_hover_option_set(game, pick_x, pick_y);

        /* Drop target: Client.ts hoveredSlot from cell geometry including empty slots — not
         * uitree_hover nor menu-filtered cache pick (interface_cache_pick_inv_at_screen). */
        int to_comp = -1;
        int to_slot = -1;
        if( game->gamecache )
        {
            struct GameCacheComponent* p = NULL;
            int ps = -1;
            if( interface_cache_pick_inv_at_screen_geom(game, pick_x, pick_y, &p, &ps) && p &&
                ps >= 0 )
            {
                to_comp = p->id;
                to_slot = ps;
            }
        }
        if( to_comp == comp && to_slot >= 0 && from_slot >= 0 && to_slot != from_slot )
        {
            int mode = 0;
            interface_inv_client_predict_drag(game, comp, from_slot, to_slot, &mode);
            gamenet_send_inv_button_d(game, comp, from_slot, to_slot, mode);
            iface->inv_suppress_next_left_click = 1;
            return;
        }
        return;
    }

    /* Gesture did not qualify as drag (no threshold / too fast): treat as a
     * regular left-click at the original grab position, matching Client.ts
     * which falls through to doAction(menuNumEntries-1) in that case. */
    minimenu_game_use_primary_at(game, input, grab_x, grab_y);
}

void
interface_magic_tab_request_inv_transmit_if_configured(struct GGame* game)
{
    if( !game || GAME_NET_STATE_GAME != game->net_state )
        return;
    int cid = game->magic_tab_inv_transmit_if_button_comp;
    if( cid <= 0 )
        return;
    gamenet_send_if_button(game, cid);
}
