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
    case UIELEM_RS_RECT:
        return "rs_rect";
    case UIELEM_RS_LINE:
        return "rs_line";
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

static int
uitree_script_length_from_opcode0(int const* script)
{
    if( !script )
        return 0;
    int pc = 0;
    for( ;; )
    {
        int opcode = script[pc++];
        if( opcode == 0 )
            return pc;
        if( opcode == 1 || opcode == 2 || opcode == 3 || opcode == 6 )
            pc += 1;
        else if( opcode == 4 || opcode == 10 )
            pc += 2;
        else if( opcode == 5 || opcode == 7 || opcode == 13 || opcode == 14 || opcode == 20 )
            pc += 1;
        else if( opcode == 15 || opcode == 16 || opcode == 17 )
            continue;
        else
            pc += 1;
    }
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
    dst->over_color = src->over_color;
    dst->active_color = src->active_color;
    dst->active_over_color = src->active_over_color;
    dst->scripts_count = src->scripts_count;

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
                      : uitree_script_length_from_opcode0(src->scripts[i]);
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
                "       rs_graphic scene_id=%d atlas=%d active_scene=%d active_atlas=%d hitbox_only=%d\n",
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
uitree_push(
    struct UITree* tree,
    int32_t parent_index,
    struct UINodeSpec const* spec)
{
    if( !tree || !spec )
        return -1;

    char* text_owned = NULL;
    if( spec->type == UIELEM_RS_TEXT && spec->u.rs_text.text )
    {
        text_owned = strdup(spec->u.rs_text.text);
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
    component->type = spec->type;
    component->component_id = spec->component_id;

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
        component->u.sidebar.inv_index = spec->u.sidebar.inv_index;
        break;

    case UIELEM_RS_LAYER:
        component->u.rs_layer.reserved = spec->u.rs_layer.reserved;
        break;

    case UIELEM_RS_TEXT:
    {
        int font_id = spec->u.rs_text.font_id;
        if( font_id < 0 || font_id > 3 )
            font_id = 1;
        component->u.rs_text.font_id = font_id;
        component->u.rs_text.color = spec->u.rs_text.color;
        component->u.rs_text.center = spec->u.rs_text.center;
        component->u.rs_text.shadowed = spec->u.rs_text.shadowed;
        component->u.rs_text.text = text_owned;
        text_owned = NULL;
        break;
    }

    case UIELEM_RS_GRAPHIC:
        component->u.rs_graphic.scene_id = spec->u.rs_graphic.scene_id;
        component->u.rs_graphic.atlas_index = spec->u.rs_graphic.atlas_index;
        component->u.rs_graphic.scene_id_active = spec->u.rs_graphic.scene_id_active;
        component->u.rs_graphic.atlas_index_active = spec->u.rs_graphic.atlas_index_active;
        component->u.rs_graphic.graphic_hitbox_only = spec->u.rs_graphic.graphic_hitbox_only;
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

    case UIELEM_RS_INV:
        component->u.rs_inv.inv_index = spec->u.rs_inv.inv_index;
        component->u.rs_inv.cols = spec->u.rs_inv.cols;
        component->u.rs_inv.rows = spec->u.rs_inv.rows;
        component->u.rs_inv.margin_x = spec->u.rs_inv.margin_x;
        component->u.rs_inv.margin_y = spec->u.rs_inv.margin_y;
        if( spec->u.rs_inv.inv_slot_offset_x && spec->u.rs_inv.inv_slot_offset_y )
        {
            memcpy(
                component->u.rs_inv.inv_slot_offset_x,
                spec->u.rs_inv.inv_slot_offset_x,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
            memcpy(
                component->u.rs_inv.inv_slot_offset_y,
                spec->u.rs_inv.inv_slot_offset_y,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
        }
        if( spec->u.rs_inv.inv_slot_bg_scene_id && spec->u.rs_inv.inv_slot_bg_atlas_index )
        {
            memcpy(
                component->u.rs_inv.inv_slot_bg_scene_id,
                spec->u.rs_inv.inv_slot_bg_scene_id,
                (size_t)UI_INV_SLOT_OFFSET_MAX * sizeof(int));
            memcpy(
                component->u.rs_inv.inv_slot_bg_atlas_index,
                spec->u.rs_inv.inv_slot_bg_atlas_index,
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
