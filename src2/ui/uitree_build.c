#include "uitree_build.h"

#include <stdlib.h>
#include <string.h>

void
uitree_fill_position_from_component(
    struct StaticUIElemPosition* pos,
    Component const* comp)
{
    if( !pos || !comp )
        return;

    pos->kind = UIPOS_XY;
    pos->x = comp->baseX;
    pos->y = comp->baseY;
    pos->width = comp->baseWidth;
    pos->height = comp->baseHeight;
    if( comp->if3 )
    {
        pos->x_mode = comp->xMode;
        pos->y_mode = comp->yMode;
        pos->width_mode = comp->widthMode;
        pos->height_mode = comp->heightMode;
        pos->aspect_w = comp->aspect_ratio_w > 0 ? comp->aspect_ratio_w : 1;
        pos->aspect_h = comp->aspect_ratio_h > 0 ? comp->aspect_ratio_h : 1;
    }
    else
    {
        pos->x_mode = -1;
        pos->y_mode = -1;
        pos->width_mode = -1;
        pos->height_mode = -1;
    }
}

int32_t
uitree_push_component(
    struct UITree* tree,
    int32_t parent_index,
    Component* comp,
    int (*resolve_sprite)(void*, int),
    void* resolve_ud)
{
    if( !tree || !comp )
        return -1;

    struct UINodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.component_id = comp->id;
    spec.has_position = 1;
    uitree_fill_position_from_component(&spec.position, comp);

    switch( comp->type )
    {
    case 5:
        spec.type = UIELEM_RS_GRAPHIC;
        spec.u.rs_graphic.scene_id = comp->graphic;
        spec.u.rs_graphic.graphic_hitbox_only = comp->graphic < 0 ? 1 : 0;
        spec.u.rs_graphic.tiled = comp->tiled ? 1 : 0;
        break;
    case 3:
        spec.type = UIELEM_RS_RECT;
        spec.u.rs_rect.color = comp->color;
        spec.u.rs_rect.filled = comp->fill ? 1 : 0;
        break;
    case 4:
        spec.type = UIELEM_RS_TEXT;
        spec.u.rs_text.font_id = comp->textFont >= 0 ? comp->textFont : 495;
        spec.u.rs_text.color = comp->color;
        spec.u.rs_text.center = comp->textHorizontalAlignment;
        spec.u.rs_text.shadowed = comp->textShadow ? 1 : 0;
        spec.u.rs_text.text = comp->text;
        break;
    case 9:
        spec.type = UIELEM_RS_LINE;
        spec.u.rs_line.color = comp->color;
        spec.u.rs_line.line_width = comp->lineWidth;
        spec.u.rs_line.horizontal = comp->lineDirection ? 1 : 0;
        break;
    case 2:
    {
        int offset_x[UI_INV_SLOT_OFFSET_MAX];
        int offset_y[UI_INV_SLOT_OFFSET_MAX];
        int bg_sid[UI_INV_SLOT_OFFSET_MAX];
        int bg_ai[UI_INV_SLOT_OFFSET_MAX];
        for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
        {
            offset_x[si] = 0;
            offset_y[si] = 0;
            bg_sid[si] = -1;
            bg_ai[si] = 0;
        }

        spec.type = UIELEM_INV_GRID;
        spec.u.inv_grid.cols = comp->baseWidth > 0 ? comp->baseWidth : 1;
        spec.u.inv_grid.rows = comp->baseHeight > 0 ? comp->baseHeight : 1;
        spec.u.inv_grid.margin_x = comp->marginX;
        spec.u.inv_grid.margin_y = comp->marginY;
        if( comp->invSlotOffsetX && comp->invSlotOffsetY )
        {
            memcpy(offset_x, comp->invSlotOffsetX, (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
            memcpy(offset_y, comp->invSlotOffsetY, (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        }
        spec.u.inv_grid.inv_slot_offset_x = offset_x;
        spec.u.inv_grid.inv_slot_offset_y = offset_y;
        if( comp->invSlotGraphicId && resolve_sprite )
        {
            for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
            {
                int const gfx = comp->invSlotGraphicId[si];
                if( gfx < 0 )
                    continue;
                int const element_id = resolve_sprite(resolve_ud, gfx);
                if( element_id >= 0 )
                    bg_sid[si] = element_id;
            }
        }
        spec.u.inv_grid.inv_slot_bg_scene_id = bg_sid;
        spec.u.inv_grid.inv_slot_bg_atlas_index = bg_ai;
        break;
    }
    case 6:
        spec.type = UIELEM_RS_MODEL;
        spec.u.rs_model.gamecache_model_id = comp->modelId;
        spec.u.rs_model.zoom = comp->modelZoom > 0 ? comp->modelZoom : 100;
        spec.u.rs_model.xan = comp->modelXAngle;
        spec.u.rs_model.yan = comp->modelYAngle;
        break;
    case 0:
    default:
        spec.type = UIELEM_RS_LAYER;
        spec.u.rs_layer.scroll_width = comp->scrollWidth;
        spec.u.rs_layer.scroll_height = comp->scrollHeight;
        break;
    }

    int32_t idx = uitree_push(tree, parent_index, &spec);
    if( idx >= 0 )
    {
        tree->components[idx].behavior.hide = comp->hidden ? 1 : 0;
        if( comp->transparency > 0 )
            tree->components[idx].trans = comp->transparency;
        tree->components[idx].drag_dead_zone = comp->dragDeadZone;
        tree->components[idx].drag_dead_time = comp->dragDeadTime;
    }
    return idx;
}

int
uitree_build_from_source(
    struct UITree* tree,
    struct UITreeBuildSource const* src)
{
    if( !tree || !src || src->count <= 0 || !src->get_component || !src->get_parent_id )
        return -1;

    int const comp_count = src->count;
    int* parent_idx = calloc((size_t)comp_count, sizeof(int));
    int* tree_idx = calloc((size_t)comp_count, sizeof(int));
    if( !parent_idx || !tree_idx )
    {
        free(parent_idx);
        free(tree_idx);
        return -1;
    }

    for( int i = 0; i < comp_count; i++ )
        parent_idx[i] = -1;

    for( int i = 0; i < comp_count; i++ )
    {
        int const parent_id = src->get_parent_id(src->ud, i);
        if( parent_id < 0 )
            continue;
        for( int j = 0; j < comp_count; j++ )
        {
            Component* comp = src->get_component(src->ud, j);
            if( comp && comp->id == parent_id )
            {
                parent_idx[i] = j;
                break;
            }
        }
    }

    bool* pushed = calloc((size_t)comp_count, sizeof(bool));
    if( !pushed )
    {
        free(parent_idx);
        free(tree_idx);
        return -1;
    }

    int pushed_count = 0;
    for( int pass = 0; pass < comp_count && pushed_count < comp_count; pass++ )
    {
        for( int i = 0; i < comp_count; i++ )
        {
            Component* comp = src->get_component(src->ud, i);
            if( !comp || comp->type < 0 || pushed[i] )
                continue;
            if( parent_idx[i] >= 0 && !pushed[parent_idx[i]] )
                continue;

            int32_t parent_tree = -1;
            if( parent_idx[i] >= 0 )
                parent_tree = tree_idx[parent_idx[i]];

            tree_idx[i] = uitree_push_component(
                tree,
                parent_tree,
                comp,
                src->resolve_sprite,
                src->ud);
            if( tree_idx[i] < 0 )
                continue;
            pushed[i] = true;
            pushed_count++;
        }
    }

    free(pushed);
    free(parent_idx);
    free(tree_idx);
    return 0;
}
