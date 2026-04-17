#include "uitree.h"

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
    case UIELEM_BUILTIN_MINIMAP:
        return "minimap";
    case UIELEM_BUILTIN_WORLD:
        return "world";
    case UIELEM_BUILTIN_SIDEBAR:
        return "sidebar";
    case UIELEM_BUILTIN_CHAT:
        return "chat";
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
    case UIELEM_RS_INV:
        return "rs_inv";
    case UIELEM_RS_LAYER:
        return "rs_layer";
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
    }
    free(tree->components);
    free(tree);
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
        case UIELEM_RS_INV:
            printf(
                "       rs_inv inv_index=%d cols=%d rows=%d margin=(%d,%d)\n",
                c->u.rs_inv.inv_index,
                c->u.rs_inv.cols,
                c->u.rs_inv.rows,
                c->u.rs_inv.margin_x,
                c->u.rs_inv.margin_y);
            for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
            {
                if( c->u.rs_inv.inv_slot_bg_scene_id[si] >= 0 )
                {
                    printf(
                        "       rs_inv slot_bg[%d] scene_id=%d atlas=%d\n",
                        si,
                        c->u.rs_inv.inv_slot_bg_scene_id[si],
                        c->u.rs_inv.inv_slot_bg_atlas_index[si]);
                }
            }
            break;
        case UIELEM_RS_GRAPHIC:
            printf(
                "       rs_graphic scene_id=%d atlas=%d active_scene=%d active_atlas=%d\n",
                c->u.rs_graphic.scene_id,
                c->u.rs_graphic.atlas_index,
                c->u.rs_graphic.scene_id_active,
                c->u.rs_graphic.atlas_index_active);
            break;
        case UIELEM_RS_TEXT:
            printf(
                "       rs_text font_id=%d color=%d center=%d text=%s\n",
                c->u.rs_text.font_id,
                c->u.rs_text.color,
                c->u.rs_text.center,
                c->u.rs_text.text ? c->u.rs_text.text : "(null)");
            break;
        case UIELEM_RS_MODEL:
            printf("       rs_model scene2_element_id=%d\n", c->u.rs_model.scene2_element_id);
            break;
        case UIELEM_BUILTIN_SPRITE:
            printf(
                "       sprite scene_id=%d atlas_index=%d\n",
                c->u.sprite.scene_id,
                c->u.sprite.atlas_index);
            break;
        case UIELEM_BUILTIN_SIDEBAR:
            printf(
                "       sidebar tabno=%d componentno=%d inv_index=%d\n",
                c->u.sidebar.tabno,
                c->u.sidebar.componentno,
                c->u.sidebar.inv_index);
            break;
        default:
            break;
        }
    }
}

int32_t
uitree_push_sprite_xy(
    struct UITree* tree,
    int32_t parent_index,
    int sprite_id,
    int atlas_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_SPRITE;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.sprite.scene_id = sprite_id;
    component->u.sprite.atlas_index = atlas_index;
    return idx;
}

int32_t
uitree_push_sprite_relative(
    struct UITree* tree,
    int32_t parent_index,
    int sprite_id,
    int atlas_index,
    int flags,
    int top,
    int right,
    int bottom,
    int left,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_SPRITE;
    component->position.kind = UIPOS_RELATIVE;
    component->position.relative_flags = flags;
    if( (flags & STATIC_UI_RELATIVE_FLAG_LEFT) != 0 )
        component->position.left = left;
    if( (flags & STATIC_UI_RELATIVE_FLAG_TOP) != 0 )
        component->position.top = top;
    if( (flags & STATIC_UI_RELATIVE_FLAG_RIGHT) != 0 )
        component->position.right = right;
    if( (flags & STATIC_UI_RELATIVE_FLAG_BOTTOM) != 0 )
        component->position.bottom = bottom;
    component->position.width = width;
    component->position.height = height;
    component->u.sprite.scene_id = sprite_id;
    component->u.sprite.atlas_index = atlas_index;
    return idx;
}

int32_t
uitree_push_world(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    uint8_t level_mask)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_WORLD;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->u.world.level_mask = level_mask;
    return idx;
}

int32_t
uitree_push_compass(
    struct UITree* tree,
    int32_t parent_index,
    int sprite_id,
    int atlas_index,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_COMPASS;
    component->position.kind = UIPOS_XY;
    component->u.sprite.scene_id = sprite_id;
    component->u.sprite.atlas_index = atlas_index;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->position.anchor_x = anchor_x;
    component->position.anchor_y = anchor_y;
    return idx;
}

int32_t
uitree_push_minimap(
    struct UITree* tree,
    int32_t parent_index,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_MINIMAP;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->position.anchor_x = anchor_x;
    component->position.anchor_y = anchor_y;
    return idx;
}

int32_t
uitree_push_redstone_tab(
    struct UITree* tree,
    int32_t parent_index,
    int tabno,
    int sprite_id,
    int atlas_index,
    int sprite_active_id,
    int atlas_active_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_REDSTONE_TAB;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.redstone_tab.tabno = tabno;
    component->u.redstone_tab.scene_id = sprite_id;
    component->u.redstone_tab.atlas_index = atlas_index;
    component->u.redstone_tab.scene_id_active = sprite_active_id;
    component->u.redstone_tab.atlas_index_active = atlas_active_index;
    return idx;
}

int32_t
uitree_push_builtin_sidebar(
    struct UITree* tree,
    int32_t parent_index,
    int tabno,
    int componentno,
    int inv_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_BUILTIN_SIDEBAR;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.sidebar.tabno = tabno;
    component->u.sidebar.componentno = componentno;
    component->u.sidebar.inv_index = inv_index;
    return idx;
}

int32_t
uitree_push_sidebar_component(
    struct UITree* tree,
    int32_t parent_index,
    int tabno,
    int componentno,
    int inv_index,
    int x,
    int y,
    int width,
    int height)
{
    return uitree_push_builtin_sidebar(
        tree, parent_index, tabno, componentno, inv_index, x, y, width, height);
}

int32_t
uitree_push_rs_layer(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_RS_LAYER;
    component->component_id = component_id;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.rs_layer.reserved = 0;
    return idx;
}

int32_t
uitree_push_rs_text(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int font_id,
    int color,
    int center,
    int shadowed,
    char const* text,
    int x,
    int y,
    int width,
    int height)
{
    char* text_owned = NULL;
    if( text )
    {
        text_owned = strdup(text);
        if( !text_owned )
            return -1;
    }

    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
    {
        free(text_owned);
        return -1;
    }
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_RS_TEXT;
    component->component_id = component_id;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.rs_text.font_id = font_id;
    component->u.rs_text.color = color;
    component->u.rs_text.center = center;
    component->u.rs_text.shadowed = shadowed;
    component->u.rs_text.text = text_owned;
    return idx;
}

int32_t
uitree_push_rs_graphic(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int scene_id,
    int atlas_index,
    int scene_id_active,
    int atlas_index_active,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_RS_GRAPHIC;
    component->component_id = component_id;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.rs_graphic.scene_id = scene_id;
    component->u.rs_graphic.atlas_index = atlas_index;
    component->u.rs_graphic.scene_id_active = scene_id_active;
    component->u.rs_graphic.atlas_index_active = atlas_index_active;
    return idx;
}

int32_t
uitree_push_rs_model(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int scene2_element_id,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_RS_MODEL;
    component->component_id = component_id;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.rs_model.scene2_element_id = scene2_element_id;
    return idx;
}

int32_t
uitree_push_rs_inv(
    struct UITree* tree,
    int32_t parent_index,
    int component_id,
    int inv_index,
    int cols,
    int rows,
    int margin_x,
    int margin_y,
    int const* inv_slot_offset_x,
    int const* inv_slot_offset_y,
    int const* inv_slot_bg_scene_id,
    int const* inv_slot_bg_atlas_index,
    int x,
    int y,
    int width,
    int height)
{
    int32_t idx = push_element(tree, parent_index);
    if( idx < 0 )
        return -1;
    struct StaticUIComponent* component = &tree->components[idx];

    component->type = UIELEM_RS_INV;
    component->component_id = component_id;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.rs_inv.inv_index = inv_index;
    component->u.rs_inv.cols = cols;
    component->u.rs_inv.rows = rows;
    component->u.rs_inv.margin_x = margin_x;
    component->u.rs_inv.margin_y = margin_y;
    if( inv_slot_offset_x && inv_slot_offset_y )
    {
        memcpy(
            component->u.rs_inv.inv_slot_offset_x,
            inv_slot_offset_x,
            (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        memcpy(
            component->u.rs_inv.inv_slot_offset_y,
            inv_slot_offset_y,
            (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
    }
    if( inv_slot_bg_scene_id && inv_slot_bg_atlas_index )
    {
        memcpy(
            component->u.rs_inv.inv_slot_bg_scene_id,
            inv_slot_bg_scene_id,
            (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        memcpy(
            component->u.rs_inv.inv_slot_bg_atlas_index,
            inv_slot_bg_atlas_index,
            (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
    }
    else
    {
        for( int i = 0; i < UI_INV_SLOT_OFFSET_MAX; i++ )
        {
            component->u.rs_inv.inv_slot_bg_scene_id[i] = -1;
            component->u.rs_inv.inv_slot_bg_atlas_index[i] = 0;
        }
    }
    return idx;
}
