#include "uitree.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t
link_under_parent(
    struct UITree* tree,
    int32_t parent_index,
    int32_t new_index)
{
    struct UITreeComponent* new_c = &tree->components[new_index];
    new_c->parent = parent_index;
    new_c->first_child = -1;
    new_c->next_sibling = -1;

    if( parent_index >= 0 && (uint32_t)parent_index >= tree->component_count )
    {
        fprintf(
            stderr,
            "uitree: invalid parent index %d for child %d (count=%u)\n",
            (int)parent_index,
            (int)new_index,
            tree->component_count);
        parent_index = -1;
        new_c->parent = -1;
    }

    if( parent_index < 0 )
    {
        new_c->parent = -1;
        if( tree->root_index < 0 )
        {
            tree->root_index = new_index;
        }
        else
        {
            int32_t walk = tree->root_index;
            while( tree->components[walk].next_sibling >= 0 )
                walk = tree->components[walk].next_sibling;
            tree->components[walk].next_sibling = new_index;
        }
        return new_index;
    }

    struct UITreeComponent* p = &tree->components[parent_index];
    if( p->first_child < 0 )
    {
        p->first_child = new_index;
    }
    else
    {
        int32_t walk = p->first_child;
        while( tree->components[walk].next_sibling >= 0 )
            walk = tree->components[walk].next_sibling;
        tree->components[walk].next_sibling = new_index;
    }
    return new_index;
}

static int32_t
push_element(
    struct UITree* tree,
    int32_t parent_index)
{
    if( tree->component_count >= tree->component_capacity )
    {
        uint32_t new_capacity = tree->component_capacity == 0 ? 16 : tree->component_capacity * 2;
        struct UITreeComponent* new_components =
            realloc(tree->components, new_capacity * sizeof(struct UITreeComponent));
        if( !new_components )
            return -1;
        tree->components = new_components;
        tree->component_capacity = new_capacity;
    }

    int32_t idx = (int32_t)tree->component_count++;
    struct UITreeComponent* component = &tree->components[idx];
    memset(component, 0, sizeof(struct UITreeComponent));
    component->parent = -1;
    component->first_child = -1;
    component->next_sibling = -1;
    component->component_id = -1;
    component->behavior.over_layer_id = -1;
    component->is_dirty = 1;

    link_under_parent(tree, parent_index, idx);
    tree->generation++;
    return idx;
}

static int
UITree_AllocateDynamicComponentId(
    struct UITree* tree,
    int iface_id)
{
    assert(tree);

    uint16_t next = tree->next_dynamic_uid;
    if( next < 0x8000u )
        next = 0x8000u;

    for( int i = 0; i < 0x8000; i++ )
    {
        uint16_t const child_id = next;
        int const uid = (iface_id << 16) | (int)child_id;
        next = (uint16_t)((child_id + 1u) & 0xffffu);
        if( next < 0x8000u )
            next = 0x8000u;
        if( UITree_FindByComponentId(tree, uid) < 0 )
        {
            tree->next_dynamic_uid = next;
            return uid;
        }
    }

    for( uint16_t child_id = 0x8000u; child_id != 0u; child_id++ )
    {
        int const uid = (iface_id << 16) | (int)child_id;
        if( UITree_FindByComponentId(tree, uid) < 0 )
            return uid;
    }
    return (iface_id << 16) | 0xffff;
}

static int32_t
UITree_ResolveComponentTarget(
    struct UITree const* tree,
    int component_id,
    int active_component)
{
    if( component_id >= 0 )
        return UITree_FindByComponentId(tree, component_id);
    if( active_component >= 0 )
        return UITree_FindByComponentId(tree, active_component);
    return -1;
}

static void
UITree_UnlinkChild(
    struct UITree* tree,
    int32_t parent_index,
    int32_t child_index)
{
    if( parent_index < 0 || child_index < 0 || (uint32_t)parent_index >= tree->component_count ||
        (uint32_t)child_index >= tree->component_count )
        return;

    struct UITreeComponent* parent = &tree->components[parent_index];
    int32_t prev = -1;
    int32_t walk = parent->first_child;
    while( walk >= 0 )
    {
        if( walk == child_index )
        {
            int32_t const next = tree->components[walk].next_sibling;
            if( prev < 0 )
                parent->first_child = next;
            else
                tree->components[prev].next_sibling = next;
            tree->components[walk].parent = -1;
            tree->components[walk].next_sibling = -1;
            parent->is_dirty = 1;
            tree->generation++;
            return;
        }
        prev = walk;
        walk = tree->components[walk].next_sibling;
    }
}

static void
UITree_UnlinkFromRootList(
    struct UITree* tree,
    int32_t child_index)
{
    assert(tree);
    assert(child_index >= 0 && (uint32_t)child_index < tree->component_count);

    if( tree->root_index < 0 )
        return;

    if( tree->root_index == child_index )
    {
        tree->root_index = tree->components[child_index].next_sibling;
        tree->components[child_index].next_sibling = -1;
        tree->components[child_index].parent = -1;
        tree->generation++;
        return;
    }

    int32_t prev = tree->root_index;
    int32_t walk = tree->components[prev].next_sibling;
    while( walk >= 0 )
    {
        if( walk == child_index )
        {
            tree->components[prev].next_sibling = tree->components[walk].next_sibling;
            tree->components[walk].next_sibling = -1;
            tree->components[walk].parent = -1;
            tree->generation++;
            return;
        }
        prev = walk;
        walk = tree->components[walk].next_sibling;
    }
}

void
UITree_Reparent(
    struct UITree* tree,
    int32_t child_index,
    int32_t new_parent_index)
{
    assert(tree);
    assert(child_index >= 0 && (uint32_t)child_index < tree->component_count);
    if( new_parent_index >= 0 )
        assert((uint32_t)new_parent_index < tree->component_count);
    assert(child_index != new_parent_index);

    struct UITreeComponent* child = &tree->components[child_index];
    int32_t old_parent = child->parent;
    if( old_parent == new_parent_index )
        return;

    if( old_parent >= 0 )
        UITree_UnlinkChild(tree, old_parent, child_index);
    else
        UITree_UnlinkFromRootList(tree, child_index);

    /* Preserve first_child subtree; only splice into new sibling list. */
    child->parent = new_parent_index;
    child->next_sibling = -1;
    child->is_dirty = 1;

    if( new_parent_index < 0 )
    {
        if( tree->root_index < 0 )
        {
            tree->root_index = child_index;
        }
        else
        {
            int32_t walk = tree->root_index;
            while( tree->components[walk].next_sibling >= 0 )
                walk = tree->components[walk].next_sibling;
            tree->components[walk].next_sibling = child_index;
        }
    }
    else
    {
        struct UITreeComponent* p = &tree->components[new_parent_index];
        if( p->first_child < 0 )
        {
            p->first_child = child_index;
        }
        else
        {
            int32_t walk = p->first_child;
            while( tree->components[walk].next_sibling >= 0 )
                walk = tree->components[walk].next_sibling;
            tree->components[walk].next_sibling = child_index;
        }
        p->is_dirty = 1;
    }
    tree->generation++;
}

static int32_t
UITree_FindDynamicChildByIndex(
    struct UITree* tree,
    int32_t parent_idx,
    int dynamic_index)
{
    assert(tree);
    if( parent_idx < 0 || (uint32_t)parent_idx >= tree->component_count )
        return -1;

    int child_index = 0;
    for( int32_t child = tree->components[parent_idx].first_child; child >= 0;
         child = tree->components[child].next_sibling, child_index++ )
    {
        if( !tree->components[child].dynamic )
            continue;
        if( child_index == dynamic_index )
            return child;
    }
    return -1;
}

char const*
UITree_ComponentTypeStr(enum UITreeComponentType type)
{
    switch( type )
    {
    case UIELEM_BUILTIN_COMPASS:
        return "compass";
    case UIELEM_BUILTIN_CROSS:
        return "cross";
    case UIELEM_BUILTIN_MINIMENU:
        return "minimenu";
    case UIELEM_BUILTIN_MINIMAP:
        return "minimap";
    case UIELEM_BUILTIN_WORLD:
        return "world";
    case UIELEM_BUILTIN_SIDEBAR:
        return "sidebar";
    case UIELEM_BUILTIN_CHAT:
        return "chat";
    case UIELEM_BUILTIN_CHAT_BUTTON:
        return "chat_button";
    case UIELEM_BUILTIN_SPRITE:
        return "sprite";
    case UIELEM_BUILTIN_REDSTONE_TAB:
        return "redstone_tab";
    case UIELEM_BUILTIN_TAB_ICONS:
        return "tab_icons";
    case UIELEM_RS_TEXT:
        return "rs_text";
    case UIELEM_RS_GRAPHIC:
        return "rs_graphic";
    case UIELEM_RS_MODEL:
        return "rs_model";
    case UIELEM_INV_GRID:
        return "inv_grid";
    case UIELEM_RS_LAYER:
        return "rs_layer";
    case UIELEM_RS_RECT:
        return "rs_rect";
    case UIELEM_RS_LINE:
        return "rs_line";
    case UIELEM_RS_INV_TEXT:
        return "rs_inv_text";
    case UIELEM_INV_SLOT:
        return "inv_slot";
    case UIELEM_CC_OBJ:
        return "cc_obj";
    }
    return "unknown";
}

struct UITree*
UITree_New(uint32_t hint)
{
    (void)hint;
    struct UITree* tree = malloc(sizeof(struct UITree));
    assert(tree);

    memset(tree, 0, sizeof(struct UITree));
    tree->root_index = -1;
    return tree;
}

void
UITree_Free(struct UITree* tree)
{
    assert(tree);

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent* c = &tree->components[i];
        if( c->type == UIELEM_RS_TEXT && c->u.rs_text.text )
            free((void*)c->u.rs_text.text);
        if( c->type == UIELEM_RS_TEXT && c->u.rs_text.text_active )
            free((void*)c->u.rs_text.text_active);
        struct UITreeBehavior* b = &c->behavior;
        if( b->scripts )
        {
            for( int s = 0; s < b->scripts_count; s++ )
                free(b->scripts[s]);
            free(b->scripts);
        }
        free(b->scripts_lengths);
        free(b->script_comparator);
        free(b->script_operand);
    }
    free(tree->components);
    free(tree);
}

void
UITree_MarkAllDirty(struct UITree* tree)
{
    assert(tree);

    for( uint32_t i = 0; i < tree->component_count; i++ )
        tree->components[i].is_dirty = 1;
}

void
UITree_MarkNodeDirty(
    struct UITree* tree,
    int32_t idx)
{
    assert(tree);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return;
    tree->components[idx].is_dirty = 1;
}

void
UITree_ClearNodeDirty(
    struct UITree* tree,
    int32_t idx)
{
    assert(tree);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return;
    tree->components[idx].is_dirty = 0;
}

bool
UITree_NodeNeedsEmit(struct UITreeComponent const* component)
{
    assert(component);
    return component->is_dirty || component->always_dirty;
}

bool
UITree_TypeIsAlwaysDirtyFrame(enum UITreeComponentType type)
{
    return type == UIELEM_BUILTIN_WORLD || type == UIELEM_BUILTIN_MINIMAP ||
           type == UIELEM_BUILTIN_COMPASS || type == UIELEM_BUILTIN_CROSS ||
           type == UIELEM_BUILTIN_MINIMENU;
}

void
UITree_MarkFrameAlwaysDirtyTypes(struct UITree* tree)
{
    assert(tree);

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent* c = &tree->components[i];
        if( UITree_TypeIsAlwaysDirtyFrame(c->type) )
        {
            c->is_dirty = 1;
            if( !c->always_dirty )
                c->always_dirty = 1;
        }
    }
}

int32_t
UITree_FindByComponentId(
    struct UITree const* tree,
    int component_id)
{
    assert(tree);
    if( component_id < 0 || !tree->components )
        return -1;
    int32_t fallback = -1;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].component_id != component_id )
            continue;
        if( tree->components[i].dynamic )
            return (int32_t)i;
        if( fallback < 0 )
            fallback = (int32_t)i;
    }
    return fallback;
}

void
UITree_WalkAdvance(
    struct UITree const* tree,
    int32_t* io_current,
    int32_t* stack,
    int* io_stack_top,
    int stack_max,
    bool current_visible)
{
    assert(tree);
    assert(io_current);
    if( *io_current < 0 )
        return;

    struct UITreeComponent const* c = &tree->components[*io_current];

    if( c->first_child >= 0 && current_visible && io_stack_top && stack &&
        *io_stack_top + 1 < stack_max )
    {
        stack[++(*io_stack_top)] = *io_current;
        *io_current = c->first_child;
        return;
    }

    if( c->next_sibling >= 0 )
    {
        *io_current = c->next_sibling;
        return;
    }

    if( !io_stack_top || !stack )
    {
        *io_current = -1;
        return;
    }

    while( *io_stack_top >= 0 )
    {
        int32_t parent_index = stack[(*io_stack_top)--];
        struct UITreeComponent const* parent = &tree->components[parent_index];
        if( parent->next_sibling >= 0 )
        {
            *io_current = parent->next_sibling;
            return;
        }
    }
    *io_current = -1;
}

void
UITree_SetBehavior(
    struct UITree* tree,
    int32_t idx,
    struct UITreeBehavior const* src)
{
    assert(tree);
    assert(src);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return;

    struct UITreeComponent* c = &tree->components[idx];
    struct UITreeBehavior* dst = &c->behavior;

    if( dst->scripts )
    {
        for( int s = 0; s < dst->scripts_count; s++ )
            free(dst->scripts[s]);
        free(dst->scripts);
    }
    free(dst->scripts_lengths);
    free(dst->script_comparator);
    free(dst->script_operand);
    memset(dst, 0, sizeof(*dst));

    dst->hide = src->hide;
    dst->button_type = src->button_type;
    dst->client_code = src->client_code;
    dst->click_mask = src->click_mask;
    dst->over_layer_id = src->over_layer_id;
    dst->over_color = src->over_color;
    dst->active_color = src->active_color;
    dst->active_over_color = src->active_over_color;
    dst->scripts_count = src->scripts_count;
    dst->script_kind = src->script_kind;

    if( src->scripts_count <= 0 || !src->scripts )
        return;

    dst->scripts = calloc((size_t)src->scripts_count, sizeof(int*));
    dst->scripts_lengths = calloc((size_t)src->scripts_count, sizeof(int));
    if( !dst->scripts || !dst->scripts_lengths )
        goto fail;

    for( int i = 0; i < src->scripts_count; i++ )
    {
        if( !src->scripts[i] )
            continue;
        int len =
            (src->scripts_lengths && src->scripts_lengths[i] > 0) ? src->scripts_lengths[i] : 0;
        if( len <= 0 )
            continue;
        dst->scripts[i] = malloc((size_t)len * sizeof(int));
        if( !dst->scripts[i] )
            goto fail;
        memcpy(dst->scripts[i], src->scripts[i], (size_t)len * sizeof(int));
        dst->scripts_lengths[i] = len;
    }

    if( src->script_comparator && src->scripts_count > 0 )
    {
        dst->script_comparator = malloc((size_t)src->scripts_count * sizeof(int));
        if( !dst->script_comparator )
            goto fail;
        memcpy(
            dst->script_comparator,
            src->script_comparator,
            (size_t)src->scripts_count * sizeof(int));
    }

    if( src->script_operand && src->scripts_count > 0 )
    {
        dst->script_operand = malloc((size_t)src->scripts_count * sizeof(int));
        if( !dst->script_operand )
            goto fail;
        memcpy(dst->script_operand, src->script_operand, (size_t)src->scripts_count * sizeof(int));
    }
    return;

fail:
    if( dst->scripts )
    {
        for( int s = 0; s < dst->scripts_count; s++ )
            free(dst->scripts[s]);
        free(dst->scripts);
    }
    free(dst->scripts_lengths);
    free(dst->script_comparator);
    free(dst->script_operand);
    memset(dst, 0, sizeof(*dst));
}

int32_t
UITree_Push(
    struct UITree* tree,
    int32_t parent_index,
    struct UITreeNodeSpec const* spec)
{
    assert(tree);
    assert(spec);

    char* text_owned = NULL;
    char* text_active_owned = NULL;
    if( spec->type == UIELEM_RS_TEXT && spec->u.rs_text.text )
    {
        text_owned = strdup(spec->u.rs_text.text);
        if( !text_owned )
            return -1;
    }
    if( spec->type == UIELEM_RS_TEXT && spec->u.rs_text.text_active )
    {
        text_active_owned = strdup(spec->u.rs_text.text_active);
        if( !text_active_owned )
        {
            free(text_owned);
            return -1;
        }
    }

    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
    {
        free(text_owned);
        free(text_active_owned);
        return -1;
    }

    struct UITreeComponent* component = &tree->components[idx];
    component->type = spec->type;
    component->component_id = spec->component_id;
    component->dynamic = spec->dynamic ? 1 : 0;
    component->dynamic_child_index = spec->dynamic ? spec->dynamic_child_index : -1;
    component->menu_options = spec->menu_options;

    if( spec->has_position )
    {
        component->position = spec->position;
        component->position.layout_resolved = 0;
        component->position.abs_x = 0;
        component->position.abs_y = 0;
        component->position.abs_w = 0;
        component->position.abs_h = 0;
    }
    else
    {
        component->position.kind = UIPOS_XY;
        component->position.x = spec->x;
        component->position.y = spec->y;
        component->position.width = spec->width;
        component->position.height = spec->height;
        component->position.anchor_x = spec->anchor_x;
        component->position.anchor_y = spec->anchor_y;
        component->position.x_mode = -1;
        component->position.y_mode = -1;
        component->position.width_mode = -1;
        component->position.height_mode = -1;
    }

    switch( spec->type )
    {
    case UIELEM_BUILTIN_SPRITE:
    case UIELEM_BUILTIN_COMPASS:
    case UIELEM_BUILTIN_CROSS:
        component->u.sprite.scene_id = spec->u.sprite.scene_id;
        component->u.sprite.atlas_index = spec->u.sprite.atlas_index;
        break;

    case UIELEM_BUILTIN_MINIMENU:
        component->u.minimenu.font_id = spec->u.minimenu.font_id;
        break;

    case UIELEM_BUILTIN_CHAT:
        component->u.chat.minimenu = spec->u.chat.minimenu;
        break;

    case UIELEM_BUILTIN_CHAT_BUTTON:
        component->u.chat_button = spec->u.chat_button;
        component->is_dirty = 1;
        break;

    case UIELEM_BUILTIN_REDSTONE_TAB:
        component->u.redstone_tab.tabno = spec->u.redstone_tab.tabno;
        component->u.redstone_tab.scene_id = spec->u.redstone_tab.scene_id;
        component->u.redstone_tab.atlas_index = spec->u.redstone_tab.atlas_index;
        component->u.redstone_tab.scene_id_active = spec->u.redstone_tab.scene_id_active;
        component->u.redstone_tab.atlas_index_active = spec->u.redstone_tab.atlas_index_active;
        break;

    case UIELEM_BUILTIN_MINIMAP:
        component->u.minimap.scene_id = spec->u.minimap.scene_id;
        break;

    case UIELEM_BUILTIN_WORLD:
        component->u.world.level_mask = spec->u.world.level_mask;
        break;

    case UIELEM_BUILTIN_SIDEBAR:
        component->u.sidebar.tabno = spec->u.sidebar.tabno;
        component->u.sidebar.componentno = spec->u.sidebar.componentno;
        component->u.sidebar.inv_source_id = spec->u.sidebar.inv_source_id;
        break;

    case UIELEM_RS_LAYER:
        component->u.rs_layer.scroll_height = spec->u.rs_layer.scroll_height;
        component->u.rs_layer.scroll_width = spec->u.rs_layer.scroll_width;
        break;

    case UIELEM_RS_TEXT:
    {
        int font_id = spec->u.rs_text.font_id;
        if( font_id < 0 )
            font_id = 1;
        component->u.rs_text.font_id = font_id;
        component->u.rs_text.color = spec->u.rs_text.color;
        component->u.rs_text.center = spec->u.rs_text.center;
        component->u.rs_text.y_align = spec->u.rs_text.y_align;
        component->u.rs_text.line_height = spec->u.rs_text.line_height;
        component->u.rs_text.shadowed = spec->u.rs_text.shadowed;
        component->u.rs_text.text = text_owned;
        component->u.rs_text.text_active = text_active_owned;
        text_owned = NULL;
        text_active_owned = NULL;
        break;
    }

    case UIELEM_RS_GRAPHIC:
        component->u.rs_graphic.scene_id = spec->u.rs_graphic.scene_id;
        component->u.rs_graphic.atlas_index = spec->u.rs_graphic.atlas_index;
        component->u.rs_graphic.scene_id_active = spec->u.rs_graphic.scene_id_active;
        component->u.rs_graphic.atlas_index_active = spec->u.rs_graphic.atlas_index_active;
        component->u.rs_graphic.graphic_hitbox_only = spec->u.rs_graphic.graphic_hitbox_only;
        component->u.rs_graphic.tiled = spec->u.rs_graphic.tiled;
        component->u.rs_graphic.outline = spec->u.rs_graphic.outline;
        component->u.rs_graphic.graphic_shadow = spec->u.rs_graphic.graphic_shadow;
        component->u.rs_graphic.flip_h = spec->u.rs_graphic.flip_h;
        component->u.rs_graphic.flip_v = spec->u.rs_graphic.flip_v;
        break;

    case UIELEM_RS_RECT:
        component->u.rs_rect.color = spec->u.rs_rect.color;
        component->u.rs_rect.filled = spec->u.rs_rect.filled;
        break;

    case UIELEM_RS_MODEL:
        component->u.rs_model.gamecache_model_id = spec->u.rs_model.gamecache_model_id;
        component->u.rs_model.zoom = spec->u.rs_model.zoom;
        component->u.rs_model.xan = spec->u.rs_model.xan;
        component->u.rs_model.yan = spec->u.rs_model.yan;
        component->u.rs_model.zan = spec->u.rs_model.zan;
        component->u.rs_model.x_offset = spec->u.rs_model.x_offset;
        component->u.rs_model.y_offset = spec->u.rs_model.y_offset;
        component->u.rs_model.orthog = spec->u.rs_model.orthog;
        component->u.rs_model.fixed_zoom = spec->u.rs_model.fixed_zoom;
        break;

    case UIELEM_INV_GRID:
        component->u.inv_grid.inv_source_id = spec->u.inv_grid.inv_source_id;
        component->u.inv_grid.cols = spec->u.inv_grid.cols;
        component->u.inv_grid.rows = spec->u.inv_grid.rows;
        component->u.inv_grid.margin_x = spec->u.inv_grid.margin_x;
        component->u.inv_grid.margin_y = spec->u.inv_grid.margin_y;
        if( spec->u.inv_grid.inv_slot_offset_x && spec->u.inv_grid.inv_slot_offset_y )
        {
            memcpy(
                component->u.inv_grid.inv_slot_offset_x,
                spec->u.inv_grid.inv_slot_offset_x,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
            memcpy(
                component->u.inv_grid.inv_slot_offset_y,
                spec->u.inv_grid.inv_slot_offset_y,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        }
        if( spec->u.inv_grid.inv_slot_bg_scene_id && spec->u.inv_grid.inv_slot_bg_atlas_index )
        {
            memcpy(
                component->u.inv_grid.inv_slot_bg_scene_id,
                spec->u.inv_grid.inv_slot_bg_scene_id,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
            memcpy(
                component->u.inv_grid.inv_slot_bg_atlas_index,
                spec->u.inv_grid.inv_slot_bg_atlas_index,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        }
        else
        {
            for( int i = 0; i < UI_INV_SLOT_OFFSET_MAX; i++ )
            {
                component->u.inv_grid.inv_slot_bg_scene_id[i] = -1;
                component->u.inv_grid.inv_slot_bg_atlas_index[i] = 0;
            }
        }
        break;

    case UIELEM_INV_SLOT:
        component->u.inv_slot.inv_source_id = spec->u.inv_slot.inv_source_id;
        component->u.inv_slot.slot = spec->u.inv_slot.slot;
        component->u.inv_slot.center_icon = spec->u.inv_slot.center_icon;
        break;

    case UIELEM_CC_OBJ:
        component->u.cc_obj.obj_id = spec->u.cc_obj.obj_id;
        component->u.cc_obj.obj_count = spec->u.cc_obj.obj_count;
        component->u.cc_obj.scene_id = spec->u.cc_obj.scene_id;
        component->u.cc_obj.atlas_index = spec->u.cc_obj.atlas_index;
        component->u.cc_obj.center_icon = spec->u.cc_obj.center_icon;
        break;

    case UIELEM_BUILTIN_TAB_ICONS:
        component->u.tab_icon.scene_id = spec->u.tab_icon.scene_id;
        component->u.tab_icon.atlas_index = spec->u.tab_icon.atlas_index;
        component->u.tab_icon.tabno = spec->u.tab_icon.tabno;
        component->is_dirty = 1;
        break;

    case UIELEM_RS_LINE:
        component->u.rs_line.color = spec->u.rs_line.color;
        component->u.rs_line.line_width =
            spec->u.rs_line.line_width > 0 ? spec->u.rs_line.line_width : 1;
        component->u.rs_line.horizontal = spec->u.rs_line.horizontal ? 1 : 0;
        break;

    case UIELEM_RS_INV_TEXT:
        component->u.rs_inv_text.inv_source_id = spec->u.rs_inv_text.inv_source_id;
        component->u.rs_inv_text.cols = spec->u.rs_inv_text.cols;
        component->u.rs_inv_text.rows = spec->u.rs_inv_text.rows;
        component->u.rs_inv_text.margin_x = spec->u.rs_inv_text.margin_x;
        component->u.rs_inv_text.margin_y = spec->u.rs_inv_text.margin_y;
        component->u.rs_inv_text.font_id = spec->u.rs_inv_text.font_id;
        component->u.rs_inv_text.color = spec->u.rs_inv_text.color;
        component->u.rs_inv_text.center = spec->u.rs_inv_text.center;
        component->u.rs_inv_text.shadowed = spec->u.rs_inv_text.shadowed;
        break;

    default:
        break;
    }

    if( spec->always_dirty )
        component->always_dirty = 1;

    if( spec->behavior )
        UITree_SetBehavior(tree, idx, spec->behavior);

    return idx;
}

void
UITree_ClearSidebarChildren(
    struct UITree* tree,
    int32_t sidebar_idx)
{
    assert(tree);
    if( sidebar_idx < 0 || (uint32_t)sidebar_idx >= tree->component_count )
        return;
    struct UITreeComponent* c = &tree->components[sidebar_idx];
    if( c->type != UIELEM_BUILTIN_SIDEBAR )
        return;
    c->first_child = -1;
    c->is_dirty = 1;
    tree->generation++;
}

int32_t
UITree_FindChildBySubid(
    struct UITree const* tree,
    int32_t parent_index,
    int parent_component_id,
    int sub_id)
{
    (void)parent_component_id;
    assert(tree);
    if( parent_index < 0 || (uint32_t)parent_index >= tree->component_count )
        return -1;

    for( int32_t child = tree->components[parent_index].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        struct UITreeComponent const* c = &tree->components[child];
        if( c->dynamic && c->dynamic_child_index == sub_id )
            return child;
        if( !c->dynamic && (c->component_id & 0xFFFF) == (sub_id & 0xFFFF) )
            return child;
    }
    return -1;
}

int32_t
UITree_CcCreate(
    struct UITree* tree,
    int32_t parent_index,
    int parent_component_id,
    int widget_type,
    int sub_id)
{
    assert(tree);
    if( parent_index < 0 || (uint32_t)parent_index >= tree->component_count )
        return -1;

    int const iface_id = parent_component_id >= 0 ? (parent_component_id >> 16) : 0;
    int const child_component_id = UITree_AllocateDynamicComponentId(tree, iface_id);

    int32_t existing = UITree_FindChildBySubid(tree, parent_index, parent_component_id, sub_id);
    if( existing >= 0 && tree->components[existing].dynamic )
        UITree_UnlinkChild(tree, parent_index, existing);

    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.component_id = child_component_id;
    spec.dynamic = 1;
    spec.dynamic_child_index = sub_id;
    spec.always_dirty = 1;
    spec.width = 0;
    spec.height = 0;

    switch( widget_type )
    {
    case 5:
        spec.type = UIELEM_RS_GRAPHIC;
        break;
    case 3:
        spec.type = UIELEM_RS_RECT;
        spec.u.rs_rect.color = 0;
        spec.u.rs_rect.filled = 1;
        break;
    case 4:
        spec.type = UIELEM_RS_TEXT;
        break;
    default:
        spec.type = UIELEM_CC_OBJ;
        break;
    }

    return UITree_Push(tree, parent_index, &spec);
}

void
UITree_CcDeleteAll(
    struct UITree* tree,
    int32_t parent_index)
{
    assert(tree);
    if( parent_index < 0 || (uint32_t)parent_index >= tree->component_count )
        return;

    struct UITreeComponent* parent = &tree->components[parent_index];
    int32_t child = parent->first_child;
    int32_t prev = -1;
    while( child >= 0 )
    {
        int32_t const next = tree->components[child].next_sibling;
        if( tree->components[child].dynamic )
        {
            if( prev < 0 )
                parent->first_child = next;
            else
                tree->components[prev].next_sibling = next;
            tree->components[child].parent = -1;
            tree->components[child].next_sibling = -1;
        }
        else
        {
            prev = child;
        }
        child = next;
    }
    parent->is_dirty = 1;
    tree->generation++;
}

int
UITree_CollectDynamicChildIndices(
    struct UITree const* tree,
    int parent_component_id,
    int start_index,
    int* out_indices,
    int out_cap)
{
    assert(tree);
    assert(out_indices);
    if( parent_component_id < 0 || out_cap <= 0 )
        return 0;

    int32_t parent_idx = UITree_FindByComponentId(tree, parent_component_id);
    if( parent_idx < 0 )
        return 0;

    int count = 0;
    for( int32_t child = tree->components[parent_idx].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        struct UITreeComponent const* c = &tree->components[child];
        if( c->dynamic_child_index <= start_index )
            continue;
        if( count < out_cap )
            out_indices[count++] = c->dynamic_child_index;
    }

    for( int i = 1; i < count; i++ )
    {
        int key = out_indices[i];
        int j = i - 1;
        while( j >= 0 && out_indices[j] > key )
        {
            out_indices[j + 1] = out_indices[j];
            j--;
        }
        out_indices[j + 1] = key;
    }
    return count;
}

bool
UITree_ApplyHide(
    struct UITree* tree,
    int component_id,
    int hide)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].behavior.hide = hide ? 1 : 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyClickMask(
    struct UITree* tree,
    int component_id,
    int32_t click_mask)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].behavior.click_mask = click_mask;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyText(
    struct UITree* tree,
    int component_id,
    char const* text)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_TEXT )
        return false;
    char* copy = strdup(text ? text : "");
    if( !copy )
        return false;

    free((void*)tree->components[idx].u.rs_text.text);
    tree->components[idx].u.rs_text.text = copy;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyGraphic(
    struct UITree* tree,
    int component_id,
    int scene_id,
    int atlas_index)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_GRAPHIC )
        return false;
    tree->components[idx].u.rs_graphic.scene_id = scene_id;
    tree->components[idx].u.rs_graphic.atlas_index = atlas_index;
    tree->components[idx].u.rs_graphic.graphic_hitbox_only = 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyColour(
    struct UITree* tree,
    int component_id,
    int colour)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    if( tree->components[idx].type == UIELEM_RS_TEXT )
        tree->components[idx].u.rs_text.color = colour;
    else if( tree->components[idx].type == UIELEM_RS_RECT )
        tree->components[idx].u.rs_rect.color = colour;
    else
        return false;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyPosition(
    struct UITree* tree,
    int component_id,
    int x,
    int y)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].position.x = x;
    tree->components[idx].position.y = y;
    tree->components[idx].position.layout_resolved = 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplySize(
    struct UITree* tree,
    int component_id,
    int width,
    int height)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].position.width = width;
    tree->components[idx].position.height = height;
    tree->components[idx].position.layout_resolved = 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyPositionModes(
    struct UITree* tree,
    int component_id,
    int x,
    int y,
    int x_mode,
    int y_mode)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].position.x = x;
    tree->components[idx].position.y = y;
    tree->components[idx].position.x_mode = (int8_t)x_mode;
    tree->components[idx].position.y_mode = (int8_t)y_mode;
    tree->components[idx].position.layout_resolved = 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplySizeModes(
    struct UITree* tree,
    int component_id,
    int width,
    int height,
    int width_mode,
    int height_mode)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].position.width = width;
    tree->components[idx].position.height = height;
    tree->components[idx].position.width_mode = (int8_t)width_mode;
    tree->components[idx].position.height_mode = (int8_t)height_mode;
    tree->components[idx].position.layout_resolved = 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyGraphicTiled(
    struct UITree* tree,
    int component_id,
    int tiled)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_GRAPHIC )
        return false;
    tree->components[idx].u.rs_graphic.tiled = tiled ? 1 : 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyGraphicOutline(
    struct UITree* tree,
    int component_id,
    int outline)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_GRAPHIC )
        return false;
    tree->components[idx].u.rs_graphic.outline = outline;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyGraphicShadow(
    struct UITree* tree,
    int component_id,
    int shadow_colour)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_GRAPHIC )
        return false;
    tree->components[idx].u.rs_graphic.graphic_shadow = shadow_colour;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyScrollSize(
    struct UITree* tree,
    int component_id,
    int scroll_width,
    int scroll_height)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_LAYER )
        return false;
    tree->components[idx].u.rs_layer.scroll_width = scroll_width;
    tree->components[idx].u.rs_layer.scroll_height = scroll_height;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyScrollPos(
    struct UITree* tree,
    int component_id,
    int scroll_x,
    int scroll_y)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].scroll_x = scroll_x;
    tree->components[idx].scroll_y = scroll_y;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyObject(
    struct UITree* tree,
    int component_id,
    int obj_id,
    int obj_count,
    int scene_id,
    int atlas_index)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;

    struct UITreeComponent* c = &tree->components[idx];
    if( obj_id > 0 && !c->dynamic && c->first_child >= 0 )
    {
        int32_t overlay_idx = UITree_FindDynamicChildByIndex(tree, idx, 1);
        if( overlay_idx >= 0 )
        {
            idx = overlay_idx;
            c = &tree->components[idx];
        }
    }

    if( obj_id <= 0 )
    {
        c->item_id = 0;
        c->item_count = 0;
        c->item_scene_id = -1;
        c->item_atlas_index = 0;
        if( c->type == UIELEM_CC_OBJ )
        {
            c->u.cc_obj.obj_id = 0;
            c->u.cc_obj.obj_count = 0;
            c->u.cc_obj.scene_id = -1;
            c->u.cc_obj.atlas_index = 0;
        }
        UITree_MarkNodeDirty(tree, idx);
        return true;
    }

    c->item_id = obj_id;
    c->item_count = obj_count > 0 ? obj_count : 1;
    c->item_scene_id = scene_id;
    c->item_atlas_index = atlas_index;

    if( c->type == UIELEM_CC_OBJ )
    {
        c->u.cc_obj.obj_id = obj_id;
        c->u.cc_obj.obj_count = c->item_count;
        c->u.cc_obj.scene_id = scene_id;
        c->u.cc_obj.atlas_index = atlas_index;
        c->u.cc_obj.center_icon = 1;
    }

    if( c->behavior.hide )
        c->behavior.hide = 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyModel(
    struct UITree* tree,
    int component_id,
    int model_id)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_MODEL )
        return false;
    tree->components[idx].u.rs_model.gamecache_model_id = model_id;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyModelTransparent(
    struct UITree* tree,
    int component_id,
    int transparent)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].model_transparent = transparent ? 1 : 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyModelAngle(
    struct UITree* tree,
    int component_id,
    int xan,
    int yan,
    int zoom)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_MODEL )
        return false;
    tree->components[idx].u.rs_model.xan = xan;
    tree->components[idx].u.rs_model.yan = yan;
    if( zoom > 0 )
        tree->components[idx].u.rs_model.zoom = zoom;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyTextFont(
    struct UITree* tree,
    int component_id,
    int font_id)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_TEXT )
        return false;
    tree->components[idx].u.rs_text.font_id = font_id;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyTextAlign(
    struct UITree* tree,
    int component_id,
    int h_align,
    int v_align,
    int line_height)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_TEXT )
        return false;
    tree->components[idx].u.rs_text.center = h_align;
    tree->components[idx].u.rs_text.y_align = v_align;
    tree->components[idx].u.rs_text.line_height = line_height;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyTextShadow(
    struct UITree* tree,
    int component_id,
    int shadowed)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_TEXT )
        return false;
    tree->components[idx].u.rs_text.shadowed = shadowed ? 1 : 0;
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

bool
UITree_ApplyTargetPriority(
    struct UITree* tree,
    int component_id,
    int priority)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].target_priority = priority;
    return true;
}

bool
UITree_ApplyRuntimeHook(
    struct UITree* tree,
    int component_id,
    struct UITreeRuntimeScriptHook* slot,
    int script_id,
    int const* argv,
    int argc)
{
    (void)tree;
    (void)component_id;
    assert(slot);

    memset(slot, 0, sizeof(*slot));
    slot->script_id = script_id;
    if( argc > 16 )
        argc = 16;
    if( argc < 0 )
        argc = 0;
    slot->argc = argc;
    if( argv && argc > 0 )
        memcpy(slot->argv, argv, (size_t)argc * sizeof(int));
    return true;
}

bool
UITree_ApplyOpBase(
    struct UITree* tree,
    int component_id,
    char const* text)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return false;
    strncpy(
        tree->components[idx].menu_options.option, text ? text : "", UITREE_MENU_OPTION_LEN - 1);
    tree->components[idx].menu_options.option[UITREE_MENU_OPTION_LEN - 1] = '\0';
    UITree_MarkNodeDirty(tree, idx);
    return true;
}

int
UITree_GetLayoutWidth(
    struct UITree const* tree,
    int component_id)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return 0;
    struct UITreeElemPosition const* pos = &tree->components[idx].position;
    if( pos->layout_resolved && pos->abs_w > 0 )
        return pos->abs_w;
    return pos->width > 0 ? pos->width : 0;
}

int
UITree_GetLayoutHeight(
    struct UITree const* tree,
    int component_id)
{
    int32_t idx = UITree_ResolveComponentTarget(tree, component_id, -1);
    if( idx < 0 )
        return 0;
    struct UITreeElemPosition const* pos = &tree->components[idx].position;
    if( pos->layout_resolved && pos->abs_h > 0 )
        return pos->abs_h;
    return pos->height > 0 ? pos->height : 0;
}

bool
UITree_ComponentVisibleById(
    struct UITreeComponent const* component,
    int hovered_component_id)
{
    assert(component);
    if( !component->behavior.hide )
        return true;
    if( component->component_id < 0 )
        return true;
    return hovered_component_id == component->component_id;
}

bool
UITree_ComponentHoveredByIds(
    int component_id,
    struct UITreeHoverIds const* hover_ids)
{
    assert(hover_ids);
    return hover_ids->main_com_id == component_id || hover_ids->side_com_id == component_id ||
           hover_ids->chat_com_id == component_id;
}

bool
UITree_ComponentVisibleByHoverIds(
    struct UITreeComponent const* component,
    struct UITreeHoverIds const* hover_ids)
{
    assert(component);
    assert(hover_ids);
    if( !component->behavior.hide )
        return true;
    if( component->component_id < 0 )
        return true;
    return UITree_ComponentHoveredByIds(component->component_id, hover_ids);
}

bool
UITree_ComponentIsClickable(struct UITreeComponent const* component)
{
    assert(component);
    return component->behavior.button_type != 0 || component->behavior.client_code > 0;
}

bool
UITree_ComponentHasMenuOptions(struct UITreeComponent const* component)
{
    assert(component);
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
    {
        if( component->menu_options.ops[i][0] != '\0' )
            return true;
    }
    return false;
}
