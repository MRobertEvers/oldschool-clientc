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

    /* Cache-decoded verb strings ride onto the node; op_actions stay 0 so the
     * menu builder derives default action ids from the slot index. */
    memcpy(spec.menu_options.option, comp->option, sizeof(spec.menu_options.option));
    memcpy(spec.menu_options.ops, comp->ops, sizeof(spec.menu_options.ops));
    memcpy(
        spec.menu_options.target_verb, comp->target_verb, sizeof(spec.menu_options.target_verb));
    memcpy(
        spec.menu_options.target_base, comp->target_base, sizeof(spec.menu_options.target_base));

    int scene_id = -1;
    int scene_id_active = -1;

    switch( comp->type )
    {
    case UIBUILD_LAYER:
        spec.type = UIELEM_RS_LAYER;
        spec.u.rs_layer.scroll_height = comp->scroll_height;
        spec.u.rs_layer.scroll_width = comp->scroll_width;
        /* The cache states this on layers and only on layers; CS2 may raise it
         * on anything later (if_/cc_setnoclickthrough). */
        spec.no_click_through = comp->no_click_through;
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
        /* Cache font id 0 (dat1 p11) is valid — resolving it uploads the
         * scene font; skipping left every p11 label invisible. */
        if( resolve_font && comp->font_id >= 0 )
        {
            int resolved = resolve_font(resolve_ud, comp->font_id);
            spec.u.rs_text.font_id = resolved >= 0 ? resolved : comp->font_id;
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
        spec.u.rs_graphic.sprite_angle_r2pi65536 = comp->sprite_angle;
        break;

    case UIBUILD_MODEL:
        spec.type = UIELEM_RS_MODEL;
        spec.u.rs_model.gamecache_model_id = comp->model_id;
        spec.u.rs_model.active_model_id = comp->model_active_id;
        spec.u.rs_model.zoom = comp->model_zoom;
        spec.u.rs_model.xan = comp->model_xan;
        spec.u.rs_model.yan = comp->model_yan;
        spec.u.rs_model.zan = comp->model_zan;
        spec.u.rs_model.x_offset = comp->model_x_offset;
        spec.u.rs_model.y_offset = comp->model_y_offset;
        spec.u.rs_model.orthog = comp->model_orthog;
        spec.u.rs_model.fixed_zoom = comp->model_fixed_zoom;
        spec.u.rs_model.anim_seq_id = comp->model_seq_id;
        spec.u.rs_model.anim_frame = 0;
        break;

    case UIBUILD_INV:
    {
        spec.type = UIELEM_RS_INV;
        spec.u.rs_inv.inv_source_id = comp->id;
        spec.u.rs_inv.cols = comp->inv_cols;
        spec.u.rs_inv.rows = comp->inv_rows;
        spec.u.rs_inv.margin_x = comp->margin_x;
        spec.u.rs_inv.margin_y = comp->margin_y;
        spec.u.rs_inv.can_drag = comp->inv_can_drag;
        spec.u.rs_inv.obj_ops = comp->inv_obj_ops;
        spec.u.rs_inv.obj_use = comp->inv_obj_use;
        spec.u.rs_inv.inv_slot_offset_x = comp->inv_slot_offset_x;
        spec.u.rs_inv.inv_slot_offset_y = comp->inv_slot_offset_y;

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
        spec.u.rs_inv.inv_slot_bg_scene_id = slot_bg_scene;
        spec.u.rs_inv.inv_slot_bg_atlas_index = slot_bg_atlas;
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
        if( resolve_font && comp->font_id >= 0 )
        {
            int resolved = resolve_font(resolve_ud, comp->font_id);
            spec.u.rs_inv_text.font_id = resolved >= 0 ? resolved : comp->font_id;
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

    /* Content-slot clientCodes outrank the widget type: the pack ships these as
     * empty layers / placeholder graphics, and their box is where the builtin
     * draws. Everything else about the node survives (hide, hooks, options,
     * layout), so a script hiding the slot also stops the builtin.
     *
     * Minimap and compass keep scene_id 0: the pack graphic is a mask/placeholder
     * (see the client-code guard in UITree_EmitFill), so the compass falls back
     * to the host's client-hardcoded sprite and the minimap to the world map the
     * host bakes. The compass keeps the pack graphic as its circular alpha mask
     * (drawn inverted: content shows where the mask is transparent). */
    switch( comp->client_code )
    {
    case UITREE_CLIENT_CODE_CONTENT_WORLD:
        spec.type = UIELEM_BUILTIN_WORLD;
        memset(&spec.u, 0, sizeof(spec.u));
        spec.u.world.level_mask = 0xF;
        /* Cache-built viewports have no revconfig section to read mmb_rotate=
         * / wheel_zoom= from, so they take the same default the RevConfig
         * component path does (both on). */
        spec.u.world.mmb_rotate = 1;
        spec.u.world.wheel_zoom = 1;
        break;
    case UITREE_CLIENT_CODE_CONTENT_MINIMAP:
        spec.type = UIELEM_BUILTIN_MINIMAP;
        memset(&spec.u, 0, sizeof(spec.u));
        spec.u.minimap.mask_scene_id = scene_id > 0 ? scene_id : 0;
        break;
    case UITREE_CLIENT_CODE_CONTENT_COMPASS:
        spec.type = UIELEM_BUILTIN_COMPASS;
        memset(&spec.u, 0, sizeof(spec.u));
        spec.u.sprite.mask_scene_id = scene_id > 0 ? scene_id : 0;
        break;
    case UITREE_CLIENT_CODE_CONTENT_WORLDMAP:
        /* The pack node is an empty layer sized by the world map's own scripts;
         * the surface behind it is the host's baked regions. */
        spec.type = UIELEM_BUILTIN_WORLDMAP;
        memset(&spec.u, 0, sizeof(spec.u));
        break;
    case UITREE_CLIENT_CODE_CONTENT_WORLDMAP_OVERVIEW:
        /* Empty layer; the host scale-blits the area's compositetexture PNG.
         * Red viewport rects are CS2 children of overview_overlay, not here. */
        spec.type = UIELEM_BUILTIN_WORLDMAP_OVERVIEW;
        memset(&spec.u, 0, sizeof(spec.u));
        break;
    default:
        break;
    }

    int32_t idx = UITree_Push(tree, parent_index, &spec);
    if( idx < 0 )
        return -1;

    struct UITreeComponent* node = &tree->components[idx];

    /* Routed through UITree_SetBehavior rather than poking fields directly so
     * the CS1 script arrays get its deep copy — the node owns them. */
    struct UITreeBehavior behavior;
    memset(&behavior, 0, sizeof(behavior));
    behavior.hide = comp->hide;
    behavior.button_type = comp->button_type;
    behavior.client_code = comp->client_code;
    behavior.click_mask = comp->click_mask;
    behavior.over_layer_id = comp->over_layer_id;
    behavior.over_color = comp->over_color;
    behavior.active_color = comp->active_color;
    behavior.active_over_color = comp->active_over_color;
    behavior.scripts_count = comp->scripts_count;
    behavior.scripts = comp->scripts;
    behavior.scripts_lengths = comp->scripts_lengths;
    behavior.comparator_count = comp->comparator_count;
    behavior.script_comparator = comp->script_comparator;
    behavior.script_operand = comp->script_operand;
    behavior.script_kind = comp->script_kind;
    UITree_SetBehavior(tree, idx, &behavior);

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

    /* Pass 1: allocate every component unlinked so forward parents exist in
     * index_map without O(n^2) root-list walks (large packs like 161). */
    for( int i = 0; i < src->count; i++ )
    {
        struct UIBuildComponent const* comp = src->get_component(src->ud, i);
        if( !comp )
            continue;

        int32_t idx = UITree_PushBuildComponent(
            tree,
            UITREE_PARENT_UNLINKED,
            comp,
            src->resolve_sprite,
            src->resolve_font,
            src->ud);
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

    /* Pass 3: link remaining orphans as roots (O(n) appends via last_root). */
    for( int i = 0; i < src->count; i++ )
    {
        int32_t idx = index_map[i];
        if( idx < 0 )
            continue;
        if( tree->components[idx].parent >= 0 )
            continue;
        /* Not yet on the root list (unlinked alloc). */
        tree->components[idx].next_sibling = -1;
        UITree_LinkUnderParent(tree, -1, idx);
    }

    free(index_map);
    return src->count;
}
