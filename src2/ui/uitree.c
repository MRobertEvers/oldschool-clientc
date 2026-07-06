#include "uitree.h"

#include "vm/cs1vm.h"

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
    struct StaticUIComponent* new_c = &tree->components[new_index];
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

    struct StaticUIComponent* p = &tree->components[parent_index];
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
        if( tree->component_capacity == 0 )
            tree->component_capacity = 16;

        uint32_t new_capacity = tree->component_capacity == 0 ? 16 : tree->component_capacity * 2;
        struct StaticUIComponent* new_components =
            realloc(tree->components, new_capacity * sizeof(struct StaticUIComponent));
        if( !new_components )
            return -1;
        tree->components = new_components;
        tree->component_capacity = new_capacity;
    }

    int32_t idx = (int32_t)tree->component_count++;
    struct StaticUIComponent* component = &tree->components[idx];
    memset(component, 0, sizeof(struct StaticUIComponent));
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

struct UIInventoryPool*
uitree_inv_pool_new(int capacity)
{
    struct UIInventoryPool* pool = malloc(sizeof(struct UIInventoryPool));
    if( !pool )
        return NULL;
    memset(pool, 0, sizeof(struct UIInventoryPool));
    pool->capacity = capacity > 0 ? capacity : 8;
    pool->inventories = calloc((size_t)pool->capacity, sizeof(struct UIInventory));
    if( !pool->inventories )
    {
        free(pool);
        return NULL;
    }
    return pool;
}

void
uitree_inv_pool_free(struct UIInventoryPool* pool)
{
    if( !pool )
        return;
    free(pool->inventories);
    free(pool);
}

int
uitree_inv_pool_find_by_name(
    struct UIInventoryPool* pool,
    char const* name)
{
    if( !pool || !name || name[0] == '\0' )
        return -1;
    for( int i = 0; i < pool->count; i++ )
    {
        if( strcmp(pool->inventories[i].name, name) == 0 )
            return i;
    }
    return -1;
}

int
uitree_inv_pool_append(
    struct UIInventoryPool* pool,
    struct UIInventory const* inv)
{
    if( !pool || !inv )
        return -1;
    if( pool->count >= pool->capacity )
    {
        int new_cap = pool->capacity * 2;
        struct UIInventory* ni =
            realloc(pool->inventories, (size_t)new_cap * sizeof(struct UIInventory));
        if( !ni )
            return -1;
        pool->inventories = ni;
        pool->capacity = new_cap;
    }
    pool->inventories[pool->count] = *inv;
    return pool->count++;
}

char const*
uitree_component_type_str(enum StaticUIComponentType type)
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
uitree_new(uint32_t hint)
{
    (void)hint;
    struct UITree* tree = malloc(sizeof(struct UITree));
    if( !tree )
        return NULL;
    memset(tree, 0, sizeof(struct UITree));
    tree->root_index = -1;
    return tree;
}

void
uitree_free(struct UITree* tree)
{
    if( !tree )
        return;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent* c = &tree->components[i];
        if( c->type == UIELEM_RS_TEXT && c->u.rs_text.text )
            free((void*)c->u.rs_text.text);
        if( c->type == UIELEM_RS_TEXT && c->u.rs_text.text_active )
            free((void*)c->u.rs_text.text_active);
        struct StaticUIBehavior* b = &c->behavior;
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
uitree_mark_all_dirty(struct UITree* tree)
{
    if( !tree )
        return;
    for( uint32_t i = 0; i < tree->component_count; i++ )
        tree->components[i].is_dirty = 1;
}

void
uitree_mark_node_dirty(
    struct UITree* tree,
    int32_t idx)
{
    if( !tree || idx < 0 || (uint32_t)idx >= tree->component_count )
        return;
    tree->components[idx].is_dirty = 1;
}

int32_t
uitree_find_by_component_id(
    struct UITree const* tree,
    int component_id)
{
    if( !tree || component_id < 0 || !tree->components )
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

static int
uitree_allocate_dynamic_component_id(
    struct UITree* tree,
    int iface_id)
{
    if( !tree )
        return -1;

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
        if( uitree_find_by_component_id(tree, uid) < 0 )
        {
            tree->next_dynamic_uid = next;
            return uid;
        }
    }

    for( uint16_t child_id = 0x8000u; child_id != 0u; child_id++ )
    {
        int const uid = (iface_id << 16) | (int)child_id;
        if( uitree_find_by_component_id(tree, uid) < 0 )
            return uid;
    }
    return (iface_id << 16) | 0xffff;
}

static int32_t
uitree_resolve_component_target(
    struct UITree const* tree,
    int component_id,
    int active_component)
{
    if( component_id >= 0 )
        return uitree_find_by_component_id(tree, component_id);
    if( active_component >= 0 )
        return uitree_find_by_component_id(tree, active_component);
    return -1;
}

void
uitree_walk_advance(
    struct UITree const* tree,
    int32_t* io_current,
    int32_t* stack,
    int* io_stack_top,
    int stack_max,
    bool current_visible)
{
    if( !tree || !io_current || *io_current < 0 )
        return;

    struct StaticUIComponent const* c = &tree->components[*io_current];

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
        struct StaticUIComponent const* parent = &tree->components[parent_index];
        if( parent->next_sibling >= 0 )
        {
            *io_current = parent->next_sibling;
            return;
        }
    }
    *io_current = -1;
}

bool
uitree_apply_hide(
    struct UITree* tree,
    int component_id,
    int hide)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].behavior.hide = hide ? 1 : 0;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_apply_click_mask(
    struct UITree* tree,
    int component_id,
    int32_t click_mask)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].behavior.click_mask = click_mask;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_apply_text(
    struct UITree* tree,
    int component_id,
    char const* text)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_TEXT )
        return false;
    char* copy = strdup(text ? text : "");
    if( !copy )
        return false;
    free((void*)tree->components[idx].u.rs_text.text);
    tree->components[idx].u.rs_text.text = copy;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_apply_graphic(
    struct UITree* tree,
    int component_id,
    int graphic_id)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_GRAPHIC )
        return false;
    tree->components[idx].u.rs_graphic.scene_id = graphic_id;
    tree->components[idx].u.rs_graphic.atlas_index = 0;
    tree->components[idx].u.rs_graphic.graphic_hitbox_only = 0;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_apply_colour(
    struct UITree* tree,
    int component_id,
    int colour)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 )
        return false;
    if( tree->components[idx].type == UIELEM_RS_TEXT )
        tree->components[idx].u.rs_text.color = colour;
    else if( tree->components[idx].type == UIELEM_RS_RECT )
        tree->components[idx].u.rs_rect.color = colour;
    else
        return false;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_apply_position(
    struct UITree* tree,
    int component_id,
    int x,
    int y)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].position.x = x;
    tree->components[idx].position.y = y;
    tree->components[idx].position.layout_resolved = 0;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_apply_size(
    struct UITree* tree,
    int component_id,
    int width,
    int height)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].position.width = width;
    tree->components[idx].position.height = height;
    tree->components[idx].position.layout_resolved = 0;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_apply_position_modes(
    struct UITree* tree,
    int component_id,
    int x,
    int y,
    int x_mode,
    int y_mode)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].position.x = x;
    tree->components[idx].position.y = y;
    tree->components[idx].position.x_mode = (int8_t)x_mode;
    tree->components[idx].position.y_mode = (int8_t)y_mode;
    tree->components[idx].position.layout_resolved = 0;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_apply_size_modes(
    struct UITree* tree,
    int component_id,
    int width,
    int height,
    int width_mode,
    int height_mode)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].position.width = width;
    tree->components[idx].position.height = height;
    tree->components[idx].position.width_mode = (int8_t)width_mode;
    tree->components[idx].position.height_mode = (int8_t)height_mode;
    tree->components[idx].position.layout_resolved = 0;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_apply_graphic_tiled(
    struct UITree* tree,
    int component_id,
    int tiled)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_GRAPHIC )
        return false;
    tree->components[idx].u.rs_graphic.tiled = tiled ? 1 : 0;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_apply_graphic_outline(
    struct UITree* tree,
    int component_id,
    int outline)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_GRAPHIC )
        return false;
    tree->components[idx].u.rs_graphic.outline = outline;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_apply_graphic_shadow(
    struct UITree* tree,
    int component_id,
    int shadow_colour)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_GRAPHIC )
        return false;
    tree->components[idx].u.rs_graphic.graphic_shadow = shadow_colour;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

int
uitree_get_layout_width(
    struct UITree const* tree,
    int component_id)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 )
        return 0;
    struct StaticUIElemPosition const* pos = &tree->components[idx].position;
    if( pos->layout_resolved && pos->abs_w > 0 )
        return pos->abs_w;
    return pos->width > 0 ? pos->width : 0;
}

int
uitree_get_layout_height(
    struct UITree const* tree,
    int component_id)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 )
        return 0;
    struct StaticUIElemPosition const* pos = &tree->components[idx].position;
    if( pos->layout_resolved && pos->abs_h > 0 )
        return pos->abs_h;
    return pos->height > 0 ? pos->height : 0;
}

bool
uitree_apply_scroll_size(
    struct UITree* tree,
    int component_id,
    int scroll_width,
    int scroll_height)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_LAYER )
        return false;
    tree->components[idx].u.rs_layer.scroll_width = scroll_width;
    tree->components[idx].u.rs_layer.scroll_height = scroll_height;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

static int32_t
uitree_find_dynamic_child_by_index(
    struct UITree* tree,
    int32_t parent_idx,
    int dynamic_index)
{
    if( !tree || parent_idx < 0 || (uint32_t)parent_idx >= tree->component_count )
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

bool
uitree_apply_object(
    struct UITree* tree,
    int component_id,
    int obj_id,
    int obj_count,
    int scene_id,
    int atlas_index)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 )
        return false;

    struct StaticUIComponent* c = &tree->components[idx];
    if( obj_id > 0 && !c->dynamic && c->first_child >= 0 )
    {
        int32_t overlay_idx = uitree_find_dynamic_child_by_index(tree, idx, 1);
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
        uitree_mark_node_dirty(tree, idx);
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
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_apply_model(
    struct UITree* tree,
    int component_id,
    int model_id)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 || tree->components[idx].type != UIELEM_RS_MODEL )
        return false;
    tree->components[idx].u.rs_model.gamecache_model_id = model_id;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_apply_model_transparent(
    struct UITree* tree,
    int component_id,
    int transparent)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 )
        return false;
    tree->components[idx].model_transparent = transparent ? 1 : 0;
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_apply_op_base(
    struct UITree* tree,
    int component_id,
    char const* text)
{
    int32_t idx = uitree_resolve_component_target(tree, component_id, -1);
    if( idx < 0 )
        return false;
    strncpy(
        tree->components[idx].menu_options.option, text ? text : "", UITREE_MENU_OPTION_LEN - 1);
    tree->components[idx].menu_options.option[UITREE_MENU_OPTION_LEN - 1] = '\0';
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_get_op(
    struct UITree const* tree,
    int component_id,
    int active_component,
    int index,
    char* out_buf,
    int out_len)
{
    if( !out_buf || out_len <= 0 )
        return false;
    out_buf[0] = '\0';
    if( !tree || index < 1 || index > UITREE_MENU_OPTION_SLOTS )
        return false;
    int32_t idx = uitree_resolve_component_target(tree, component_id, active_component);
    if( idx < 0 )
        return false;
    strncpy(out_buf, tree->components[idx].menu_options.ops[index - 1], out_len - 1);
    out_buf[out_len - 1] = '\0';
    return true;
}

bool
uitree_get_op_base(
    struct UITree const* tree,
    int component_id,
    int active_component,
    char* out_buf,
    int out_len)
{
    if( !out_buf || out_len <= 0 )
        return false;
    out_buf[0] = '\0';
    if( !tree )
        return false;
    int32_t idx = uitree_resolve_component_target(tree, component_id, active_component);
    if( idx < 0 )
        return false;
    strncpy(out_buf, tree->components[idx].menu_options.option, out_len - 1);
    out_buf[out_len - 1] = '\0';
    return true;
}

bool
uitree_clear_ops(
    struct UITree* tree,
    int component_id,
    int active_component)
{
    if( !tree )
        return false;
    int32_t idx = uitree_resolve_component_target(tree, component_id, active_component);
    if( idx < 0 )
        return false;
    for( int i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
        tree->components[idx].menu_options.ops[i][0] = '\0';
    uitree_mark_node_dirty(tree, idx);
    return true;
}

bool
uitree_get_text(
    struct UITree const* tree,
    int component_id,
    int active_component,
    char* out_buf,
    int out_len)
{
    if( !out_buf || out_len <= 0 )
        return false;
    out_buf[0] = '\0';
    if( !tree )
        return false;
    int32_t idx = uitree_resolve_component_target(tree, component_id, active_component);
    if( idx < 0 )
        return false;
    char const* text = "";
    if( tree->components[idx].type == UIELEM_RS_TEXT )
        text = tree->components[idx].u.rs_text.text;
    strncpy(out_buf, text ? text : "", (size_t)(out_len - 1));
    out_buf[out_len - 1] = '\0';
    return true;
}

int32_t
uitree_find_child_by_subid(
    struct UITree const* tree,
    int32_t parent_index,
    int parent_component_id,
    int sub_id)
{
    (void)parent_component_id;
    if( !tree || parent_index < 0 || (uint32_t)parent_index >= tree->component_count )
        return -1;

    for( int32_t child = tree->components[parent_index].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        struct StaticUIComponent const* c = &tree->components[child];
        if( c->dynamic && c->dynamic_child_index == sub_id )
            return child;
        if( !c->dynamic && (c->component_id & 0xFFFF) == (sub_id & 0xFFFF) )
            return child;
    }
    return -1;
}

static void
uitree_unlink_child(
    struct UITree* tree,
    int32_t parent_index,
    int32_t child_index)
{
    if( !tree || parent_index < 0 || child_index < 0 ||
        (uint32_t)parent_index >= tree->component_count ||
        (uint32_t)child_index >= tree->component_count )
        return;

    struct StaticUIComponent* parent = &tree->components[parent_index];
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

int32_t
uitree_cc_create(
    struct UITree* tree,
    int32_t parent_index,
    int parent_component_id,
    int widget_type,
    int sub_id)
{
    if( !tree || parent_index < 0 || (uint32_t)parent_index >= tree->component_count )
        return -1;

    int const iface_id = parent_component_id >= 0 ? (parent_component_id >> 16) : 0;
    int const child_component_id = uitree_allocate_dynamic_component_id(tree, iface_id);

    int32_t existing = uitree_find_child_by_subid(tree, parent_index, parent_component_id, sub_id);
    if( existing >= 0 && tree->components[existing].dynamic )
        uitree_unlink_child(tree, parent_index, existing);

    struct UINodeSpec spec;
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

    return uitree_push(tree, parent_index, &spec);
}

void
uitree_cc_delete_all(
    struct UITree* tree,
    int32_t parent_index)
{
    if( !tree || parent_index < 0 || (uint32_t)parent_index >= tree->component_count )
        return;

    struct StaticUIComponent* parent = &tree->components[parent_index];
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

void
uitree_set_behavior(
    struct UITree* tree,
    int32_t idx,
    struct StaticUIBehavior const* src)
{
    if( !tree || idx < 0 || (uint32_t)idx >= tree->component_count || !src )
        return;

    struct StaticUIComponent* c = &tree->components[idx];
    struct StaticUIBehavior* dst = &c->behavior;

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
    dst->over_layer_id = src->over_layer_id;
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
        int len = src->scripts_lengths && src->scripts_lengths[i] > 0
                      ? src->scripts_lengths[i]
                      : cs1vm_script_length(src->scripts[i]);
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

void
uitree_print_nodes(struct UITree const* tree)
{
    return;
    if( !tree )
    {
        printf("uitree_print_nodes: tree is NULL\n");
        return;
    }
    printf("uitree: %u nodes, root_index=%d\n", tree->component_count, (int)tree->root_index);
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent const* c = &tree->components[i];
        printf(
            "  [%u] type=%s parent=%d first_child=%d next_sibling=%d component_id=%d "
            "pos kind=%d xy=(%d,%d) wh=(%d,%d)\n",
            i,
            uitree_component_type_str(c->type),
            (int)c->parent,
            (int)c->first_child,
            (int)c->next_sibling,
            (int)c->component_id,
            (int)c->position.kind,
            c->position.x,
            c->position.y,
            c->position.width,
            c->position.height);
        switch( c->type )
        {
        case UIELEM_INV_GRID:
            printf(
                "       inv_grid source_id=%d cols=%d rows=%d margin=(%d,%d)\n",
                c->u.inv_grid.inv_source_id,
                c->u.inv_grid.cols,
                c->u.inv_grid.rows,
                c->u.inv_grid.margin_x,
                c->u.inv_grid.margin_y);
            for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
            {
                if( c->u.inv_grid.inv_slot_bg_scene_id[si] >= 0 )
                {
                    printf(
                        "       inv_grid slot_bg[%d] scene_id=%d atlas=%d\n",
                        si,
                        c->u.inv_grid.inv_slot_bg_scene_id[si],
                        c->u.inv_grid.inv_slot_bg_atlas_index[si]);
                }
            }
            break;
        case UIELEM_INV_SLOT:
            printf(
                "       inv_slot source_id=%d slot=%d center=%d\n",
                c->u.inv_slot.inv_source_id,
                c->u.inv_slot.slot,
                (int)c->u.inv_slot.center_icon);
            break;
        case UIELEM_RS_GRAPHIC:
            printf(
                "       rs_graphic scene_id=%d atlas=%d active_scene=%d active_atlas=%d "
                "hitbox_only=%d\n",
                c->u.rs_graphic.scene_id,
                c->u.rs_graphic.atlas_index,
                c->u.rs_graphic.scene_id_active,
                c->u.rs_graphic.atlas_index_active,
                (int)c->u.rs_graphic.graphic_hitbox_only);
            break;
        case UIELEM_RS_TEXT:
            printf(
                "       rs_text font_id=%d color=%d center=%d text=%s\n",
                c->u.rs_text.font_id,
                c->u.rs_text.color,
                c->u.rs_text.center,
                c->u.rs_text.text ? c->u.rs_text.text : "(null)");
            break;
        case UIELEM_RS_INV_TEXT:
            printf(
                "       rs_inv_text source_id=%d cols=%d rows=%d font_id=%d color=%d\n",
                c->u.rs_inv_text.inv_source_id,
                c->u.rs_inv_text.cols,
                c->u.rs_inv_text.rows,
                c->u.rs_inv_text.font_id,
                c->u.rs_inv_text.color);
            break;
        case UIELEM_RS_MODEL:
            printf(
                "       rs_model gamecache_model_id=%d zoom=%d\n",
                c->u.rs_model.gamecache_model_id,
                c->u.rs_model.zoom);
            break;
        case UIELEM_BUILTIN_SPRITE:
            printf(
                "       sprite scene_id=%d atlas_index=%d\n",
                c->u.sprite.scene_id,
                c->u.sprite.atlas_index);
            break;
        case UIELEM_BUILTIN_SIDEBAR:
            printf(
                "       sidebar tabno=%d componentno=%d inv_source_id=%d\n",
                c->u.sidebar.tabno,
                c->u.sidebar.componentno,
                c->u.sidebar.inv_source_id);
            break;
        default:
            break;
        }
    }
}

int32_t
uitree_push(
    struct UITree* tree,
    int32_t parent_index,
    struct UINodeSpec const* spec)
{
    if( !tree || !spec )
        return -1;

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

    struct StaticUIComponent* component = &tree->components[idx];
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
        uitree_set_behavior(tree, idx, spec->behavior);

    return idx;
}

void
uitree_clear_sidebar_children(
    struct UITree* tree,
    int32_t sidebar_idx)
{
    if( !tree || sidebar_idx < 0 || (uint32_t)sidebar_idx >= tree->component_count )
        return;
    struct StaticUIComponent* c = &tree->components[sidebar_idx];
    if( c->type != UIELEM_BUILTIN_SIDEBAR )
        return;
    c->first_child = -1;
    c->is_dirty = 1;
    tree->generation++;
}
