#include "uitree_build.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
UITree_FillPositionFromBuild(
    struct UITreeElemPosition* pos,
    struct UIBuildComponent const* comp)
{
    assert(pos);
    assert(comp);

    memset(pos, 0, sizeof(*pos));
    pos->kind = UIPOS_XY;
    pos->x = comp->base_x;
    pos->y = comp->base_y;
    pos->width = comp->base_width;
    pos->height = comp->base_height;
    pos->x_mode = comp->x_mode;
    pos->y_mode = comp->y_mode;
    pos->width_mode = comp->width_mode;
    pos->height_mode = comp->height_mode;
    pos->aspect_w = comp->aspect_w;
    pos->aspect_h = comp->aspect_h;
}

int32_t
UITree_PushBuildComponent(
    struct UITree* tree,
    int32_t parent_index,
    struct UIBuildComponent const* comp,
    int (*resolve_sprite)(void*, int),
    int (*resolve_font)(void*, int),
    void* resolve_ud)
{
    assert(tree);
    assert(comp);

    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.component_id = comp->id;

    UITree_FillPositionFromBuild(&spec.position, comp);
    spec.has_position = 1;

    int scene_id = -1;
    int scene_id_active = -1;

    switch( comp->type )
    {
    case UIBUILD_LAYER:
        spec.type = UIELEM_RS_LAYER;
        spec.u.rs_layer.scroll_height = comp->scroll_height;
        spec.u.rs_layer.scroll_width = comp->scroll_width;
        break;

    case UIBUILD_RECT:
        spec.type = UIELEM_RS_RECT;
        spec.u.rs_rect.color = comp->color;
        spec.u.rs_rect.filled = comp->filled;
        break;

    case UIBUILD_TEXT:
        spec.type = UIELEM_RS_TEXT;
        spec.u.rs_text.color = comp->color;
        spec.u.rs_text.center = comp->text_h_align;
        spec.u.rs_text.y_align = comp->text_v_align;
        spec.u.rs_text.line_height = comp->text_line_height;
        spec.u.rs_text.shadowed = comp->shadowed;
        spec.u.rs_text.text = comp->text;
        spec.u.rs_text.text_active = comp->text_active;
        if( resolve_font && comp->font_id > 0 )
        {
            int resolved = resolve_font(resolve_ud, comp->font_id);
            spec.u.rs_text.font_id = resolved > 0 ? resolved : comp->font_id;
        }
        else
        {
            spec.u.rs_text.font_id = comp->font_id;
        }
        break;

    case UIBUILD_GRAPHIC:
        spec.type = UIELEM_RS_GRAPHIC;
        if( resolve_sprite && comp->graphic > 0 )
            scene_id = resolve_sprite(resolve_ud, comp->graphic);
        if( resolve_sprite && comp->graphic_active > 0 )
            scene_id_active = resolve_sprite(resolve_ud, comp->graphic_active);
        spec.u.rs_graphic.scene_id = scene_id;
        spec.u.rs_graphic.atlas_index = 0;
        spec.u.rs_graphic.scene_id_active = scene_id_active;
        spec.u.rs_graphic.atlas_index_active = 0;
        spec.u.rs_graphic.graphic_hitbox_only = comp->graphic_hitbox_only;
        spec.u.rs_graphic.tiled = comp->tiled;
        spec.u.rs_graphic.outline = comp->outline;
        spec.u.rs_graphic.graphic_shadow = comp->graphic_shadow;
        spec.u.rs_graphic.flip_h = comp->horizontal_flip;
        spec.u.rs_graphic.flip_v = comp->vertical_flip;
        break;

    case UIBUILD_MODEL:
        spec.type = UIELEM_RS_MODEL;
        spec.u.rs_model.gamecache_model_id = comp->model_id;
        spec.u.rs_model.zoom = comp->model_zoom;
        spec.u.rs_model.xan = comp->model_xan;
        spec.u.rs_model.yan = comp->model_yan;
        spec.u.rs_model.zan = comp->model_zan;
        spec.u.rs_model.x_offset = comp->model_x_offset;
        spec.u.rs_model.y_offset = comp->model_y_offset;
        spec.u.rs_model.orthog = comp->model_orthog;
        spec.u.rs_model.fixed_zoom = comp->model_fixed_zoom;
        break;

    case UIBUILD_INV:
    {
        spec.type = UIELEM_INV_GRID;
        spec.u.inv_grid.inv_source_id = comp->id;
        spec.u.inv_grid.cols = comp->inv_cols;
        spec.u.inv_grid.rows = comp->inv_rows;
        spec.u.inv_grid.margin_x = comp->margin_x;
        spec.u.inv_grid.margin_y = comp->margin_y;
        spec.u.inv_grid.inv_slot_offset_x = comp->inv_slot_offset_x;
        spec.u.inv_grid.inv_slot_offset_y = comp->inv_slot_offset_y;

        int slot_bg_scene[UI_INV_SLOT_OFFSET_MAX];
        int slot_bg_atlas[UI_INV_SLOT_OFFSET_MAX];
        for( int i = 0; i < UI_INV_SLOT_OFFSET_MAX; i++ )
        {
            if( resolve_sprite && comp->inv_slot_graphic_id[i] > 0 )
                slot_bg_scene[i] = resolve_sprite(resolve_ud, comp->inv_slot_graphic_id[i]);
            else
                slot_bg_scene[i] = -1;
            slot_bg_atlas[i] = 0;
        }
        spec.u.inv_grid.inv_slot_bg_scene_id = slot_bg_scene;
        spec.u.inv_grid.inv_slot_bg_atlas_index = slot_bg_atlas;
        break;
    }

    case UIBUILD_INV_TEXT:
        spec.type = UIELEM_RS_INV_TEXT;
        spec.u.rs_inv_text.inv_source_id = comp->id;
        spec.u.rs_inv_text.cols = comp->inv_cols;
        spec.u.rs_inv_text.rows = comp->inv_rows;
        spec.u.rs_inv_text.margin_x = comp->margin_x;
        spec.u.rs_inv_text.margin_y = comp->margin_y;
        spec.u.rs_inv_text.color = comp->color;
        spec.u.rs_inv_text.center = comp->text_h_align;
        spec.u.rs_inv_text.shadowed = comp->shadowed;
        if( resolve_font && comp->font_id > 0 )
        {
            int resolved = resolve_font(resolve_ud, comp->font_id);
            spec.u.rs_inv_text.font_id = resolved > 0 ? resolved : comp->font_id;
        }
        else
        {
            spec.u.rs_inv_text.font_id = comp->font_id;
        }
        break;

    case UIBUILD_LINE:
        spec.type = UIELEM_RS_LINE;
        spec.u.rs_line.color = comp->color;
        spec.u.rs_line.line_width = comp->line_width > 0 ? comp->line_width : 1;
        spec.u.rs_line.horizontal = comp->line_horizontal ? 1 : 0;
        break;
    }

    int32_t idx = UITree_Push(tree, parent_index, &spec);
    if( idx < 0 )
        return -1;

    struct UITreeComponent* node = &tree->components[idx];
    node->behavior.hide = comp->hide;
    node->behavior.button_type = comp->button_type;
    node->behavior.client_code = comp->client_code;
    node->behavior.click_mask = comp->click_mask;
    node->behavior.over_layer_id = comp->over_layer_id;
    node->behavior.over_color = comp->over_color;
    node->behavior.active_color = comp->active_color;
    node->behavior.active_over_color = comp->active_over_color;
    node->trans = comp->transparency;
    node->if3 = comp->if3 ? 1 : 0;
    node->drag_dead_zone = comp->drag_dead_zone;
    node->drag_dead_time = comp->drag_dead_time;
    if( comp->drag_dead_zone || comp->drag_dead_time )
        node->draggable = 1;

    return idx;
}

int
UITree_BuildFromSource(
    struct UITree* tree,
    struct UITreeBuildSource const* src)
{
    assert(tree);
    assert(src);

    if( src->count <= 0 )
        return 0;

    int32_t* index_map = (int32_t*)calloc((size_t)src->count, sizeof(int32_t));
    if( !index_map )
        return -1;

    for( int i = 0; i < src->count; i++ )
        index_map[i] = -1;

    /* Pass 1: insert every component as a root so forward layer parents exist in
     * index_map before linking (iface packs often parent earlier files to later
     * layers, e.g. 161 file 57 → layer 97). */
    for( int i = 0; i < src->count; i++ )
    {
        struct UIBuildComponent const* comp = src->get_component(src->ud, i);
        if( !comp )
            continue;

        int32_t idx = UITree_PushBuildComponent(
            tree, -1, comp, src->resolve_sprite, src->resolve_font, src->ud);
        index_map[i] = idx;
    }

    /* Pass 2: resolve parents across the full source (and existing tree ids). */
    for( int i = 0; i < src->count; i++ )
    {
        if( index_map[i] < 0 )
            continue;

        int parent_id = src->get_parent_id(src->ud, i);
        if( parent_id < 0 )
            continue;

        int32_t parent_tree_idx = -1;
        for( int j = 0; j < src->count; j++ )
        {
            struct UIBuildComponent const* candidate = src->get_component(src->ud, j);
            if( candidate && candidate->id == parent_id )
            {
                parent_tree_idx = index_map[j];
                break;
            }
        }
        if( parent_tree_idx < 0 )
            parent_tree_idx = UITree_FindByComponentId(tree, parent_id);
        if( parent_tree_idx < 0 )
            continue;

        UITree_Reparent(tree, index_map[i], parent_tree_idx);
    }

    free(index_map);
    return src->count;
}
