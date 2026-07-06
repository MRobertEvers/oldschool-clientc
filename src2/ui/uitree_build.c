#include "uitree_build.h"

#include <stdlib.h>
#include <string.h>

void
uitree_fill_position_from_component(
    struct StaticUIElemPosition* pos,
    struct ToriAuxLibCore_Component const* comp)
{
    if( !pos || !comp )
        return;

    pos->kind = UIPOS_XY;
    pos->x = comp->base_x;
    pos->y = comp->base_y;
    pos->width = comp->base_width;
    pos->height = comp->base_height;
    if( comp->if3 )
    {
        pos->x_mode = comp->x_mode;
        pos->y_mode = comp->y_mode;
        pos->width_mode = comp->width_mode;
        pos->height_mode = comp->height_mode;
        pos->aspect_w = comp->aspect_w > 0 ? comp->aspect_w : 1;
        pos->aspect_h = comp->aspect_h > 0 ? comp->aspect_h : 1;
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
    struct ToriAuxLibCore_Component* comp,
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
    case TORIAUXLIBCORE_COMPONENT_GRAPHIC:
        spec.type = UIELEM_RS_GRAPHIC;
        spec.u.rs_graphic.scene_id = comp->graphic;
        spec.u.rs_graphic.graphic_hitbox_only = comp->graphic < 0 ? 1 : 0;
        spec.u.rs_graphic.tiled = comp->tiled ? 1 : 0;
        break;
    case TORIAUXLIBCORE_COMPONENT_RECT:
        spec.type = UIELEM_RS_RECT;
        spec.u.rs_rect.color = comp->color;
        spec.u.rs_rect.filled = comp->filled ? 1 : 0;
        break;
    case TORIAUXLIBCORE_COMPONENT_TEXT:
        spec.type = UIELEM_RS_TEXT;
        spec.u.rs_text.font_id = comp->font_id >= 0 ? comp->font_id : 495;
        spec.u.rs_text.color = comp->color;
        spec.u.rs_text.center = comp->text_h_align;
        spec.u.rs_text.shadowed = comp->shadowed ? 1 : 0;
        spec.u.rs_text.text = comp->text[0] != '\0' ? comp->text : NULL;
        break;
    case TORIAUXLIBCORE_COMPONENT_LINE:
        spec.type = UIELEM_RS_LINE;
        spec.u.rs_line.color = comp->color;
        spec.u.rs_line.line_width = comp->line_width;
        spec.u.rs_line.horizontal = comp->line_horizontal ? 1 : 0;
        break;
    case TORIAUXLIBCORE_COMPONENT_INV:
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
        spec.u.inv_grid.cols = comp->inv_cols > 0 ? comp->inv_cols : 1;
        spec.u.inv_grid.rows = comp->inv_rows > 0 ? comp->inv_rows : 1;
        spec.u.inv_grid.margin_x = comp->margin_x;
        spec.u.inv_grid.margin_y = comp->margin_y;
        memcpy(offset_x, comp->inv_slot_offset_x, (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        memcpy(offset_y, comp->inv_slot_offset_y, (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        if( resolve_sprite )
        {
            for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
            {
                int const gfx = comp->inv_slot_graphic_id[si];
                if( gfx < 0 )
                    continue;
                int const element_id = resolve_sprite(resolve_ud, gfx);
                if( element_id >= 0 )
                    bg_sid[si] = element_id;
            }
        }
        spec.u.inv_grid.inv_slot_offset_x = offset_x;
        spec.u.inv_grid.inv_slot_offset_y = offset_y;
        spec.u.inv_grid.inv_slot_bg_scene_id = bg_sid;
        spec.u.inv_grid.inv_slot_bg_atlas_index = bg_ai;
        break;
    }
    case TORIAUXLIBCORE_COMPONENT_MODEL:
        spec.type = UIELEM_RS_MODEL;
        spec.u.rs_model.gamecache_model_id = comp->model_id;
        spec.u.rs_model.zoom = comp->model_zoom > 0 ? comp->model_zoom : 100;
        spec.u.rs_model.xan = comp->model_xan;
        spec.u.rs_model.yan = comp->model_yan;
        break;
    case TORIAUXLIBCORE_COMPONENT_LAYER:
    default:
        spec.type = UIELEM_RS_LAYER;
        spec.u.rs_layer.scroll_width = comp->scroll_width;
        spec.u.rs_layer.scroll_height = comp->scroll_height;
        break;
    }

    int32_t idx = uitree_push(tree, parent_index, &spec);
    if( idx >= 0 )
    {
        tree->components[idx].behavior.hide = comp->hide ? 1 : 0;
        if( comp->transparency > 0 )
            tree->components[idx].trans = comp->transparency;
        tree->components[idx].drag_dead_zone = comp->drag_dead_zone;
        tree->components[idx].drag_dead_time = comp->drag_dead_time;
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
            struct ToriAuxLibCore_Component* comp = src->get_component(src->ud, j);
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
            struct ToriAuxLibCore_Component* comp = src->get_component(src->ud, i);
            if( !comp || pushed[i] )
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
