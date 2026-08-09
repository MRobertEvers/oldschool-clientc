#!/usr/bin/env python3
"""Patch interface.c: includes, layer wrapper, hover/scrollbar/button helpers, draw helpers."""
from pathlib import Path

path = Path("src/osrs/interface.c")
text = path.read_text()

if '#include "osrs/interface_state.h"' not in text:
    text = text.replace(
        '#include "interface.h"\n',
        '#include "interface.h"\n\n#include "osrs/interface_state.h"\n#include "osrs/revconfig/uiscene.h"\n',
        1,
    )

layer_fn = """
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

"""

anchor = "\nstatic void\nfind_hovered_interface_id_recursive("
if layer_fn.strip() not in text:
    text = text.replace(anchor, "\n" + layer_fn + anchor.lstrip("\n"), 1)

# Replace interface_find_hovered_interface_id body
import re

pat_hov = re.compile(
    r"(int\ninterface_find_hovered_interface_id\([^)]+\)\n\{[^}]*)(int id = -1;\s*// find_hovered.*?\n\s*return id;\n)",
    re.DOTALL,
)
text, n = pat_hov.subn(
    r"\1if( !game->iface )\n        return -1;\n    int id = -1;\n    find_hovered_interface_id_recursive(game, root, root_x, root_y, 0, mouse_x, mouse_y, &id);\n    return id;\n",
    text,
    count=1,
)
if n != 1:
    raise SystemExit("interface_find_hovered replace failed")

# find_scrollbar_at_recursive full body
sb_fn = r"static int\nfind_scrollbar_at_recursive\([\s\S]*?^}"
sb_new = """static int
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
}"""
text, c = re.subn(sb_fn, sb_new, text, count=1, flags=re.MULTILINE)
if c != 1:
    raise SystemExit("scrollbar recursive replace failed")

btn_fn = r"static int\nfind_button_click_at_recursive\([\s\S]*?^    // return found;\n^}"
btn_new = """static int
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
}"""
text, c = re.subn(btn_fn, btn_new, text, count=1, flags=re.MULTILINE)
if c != 1:
    raise SystemExit("button recursive replace failed")

# interface_handle_scrollbar_arrow_step
h = re.compile(
    r"void\ninterface_handle_scrollbar_arrow_step\([\s\S]*?^}\n\nvoid\ninterface_draw_component_rect",
    re.MULTILINE,
)
h_new = """void
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
interface_draw_component_rect"""
text, c = h.subn(h_new, text, count=1)
if c != 1:
    raise SystemExit("scrollbar step replace failed")

# draw rect
rect_pat = re.compile(
    r"void\ninterface_draw_component_rect\([\s\S]*?^}\n\nvoid\ninterface_draw_component_text",
    re.MULTILINE,
)
rect_new = r"""void
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
interface_draw_component_text"""
text, c = rect_pat.subn(rect_new, text, count=1)
if c != 1:
    raise SystemExit("rect replace failed")

path.write_text(text)
print("patch_interface_remaining.py: phase 1 ok")
