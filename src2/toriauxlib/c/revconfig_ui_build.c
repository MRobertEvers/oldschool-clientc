#include "revconfig_ui_build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static enum StaticUIComponentType
component_type_from_string(char const* type)
{
    if( !type )
        return UIELEM_BUILTIN_SPRITE;
    if( strcmp(type, "compass") == 0 )
        return UIELEM_BUILTIN_COMPASS;
    if( strcmp(type, "minimap") == 0 )
        return UIELEM_BUILTIN_MINIMAP;
    if( strcmp(type, "world") == 0 )
        return UIELEM_BUILTIN_WORLD;
    if( strcmp(type, "sidebar") == 0 )
        return UIELEM_BUILTIN_SIDEBAR;
    if( strcmp(type, "chat") == 0 )
        return UIELEM_BUILTIN_CHAT;
    if( strcmp(type, "sprite") == 0 )
        return UIELEM_BUILTIN_SPRITE;
    if( strcmp(type, "redstone_tab") == 0 )
        return UIELEM_BUILTIN_REDSTONE_TAB;
    if( strcmp(type, "builtin_tab_icons") == 0 )
        return UIELEM_BUILTIN_TAB_ICONS;
    if( strcmp(type, "rs_graphic") == 0 )
        return UIELEM_RS_GRAPHIC;
    if( strcmp(type, "rs_layer") == 0 )
        return UIELEM_RS_LAYER;
    if( strcmp(type, "rs_text") == 0 )
        return UIELEM_RS_TEXT;
    if( strcmp(type, "rs_rect") == 0 )
        return UIELEM_RS_RECT;
    return UIELEM_BUILTIN_SPRITE;
}

static uint8_t
parse_paint_levels_mask(char const* str)
{
    if( !str || str[0] == '\0' )
        return 0xFu;
    unsigned m = 0u;
    char const* p = str;
    while( *p != '\0' )
    {
        while( *p == ' ' || *p == '\t' )
            p++;
        if( *p == '\0' )
            break;
        char* end = NULL;
        long lo = strtol(p, &end, 10);
        if( end == p )
        {
            while( *p && *p != ',' )
                p++;
            if( *p == ',' )
                p++;
            continue;
        }
        p = end;
        if( lo >= 0 && lo < 8 )
            m |= 1u << (unsigned)lo;
        while( *p == ' ' || *p == '\t' )
            p++;
        if( *p == ',' )
            p++;
    }
    return m == 0u ? 0xFu : (uint8_t)m;
}

void
revconfig_ui_build_init(struct RevConfigUIBuildState* state)
{
    if( !state )
        return;
    memset(state, 0, sizeof(*state));
    state->next_element_id = 1;
}

void
revconfig_ui_build_collect_items(
    struct RevConfigUIBuildState* state,
    struct RevConfigItemBuffer const* items)
{
    if( !state || !items )
        return;

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        switch( item->kind )
        {
        case RCITEM_CACHE:
            if( state->sprite_ref_count < REVCONFIG_UI_MAX_SPRITE_REFS )
            {
                struct RevConfigUISpriteRef* ref = &state->sprite_refs[state->sprite_ref_count++];
                strncpy(ref->name, item->u.cache.name, sizeof(ref->name) - 1);
                ref->cache_item = item->u.cache;
                ref->element_id = state->next_element_id++;
            }
            break;
        case RCITEM_UICOMPONENT:
            if( state->component_count < 128 )
                state->components[state->component_count++] = item->u.uicomponent;
            break;
        case RCITEM_UILAYOUT:
            if( state->layout_entry_count < 128 )
                state->layout_entries[state->layout_entry_count++] = item->u.uilayout;
            break;
        default:
            break;
        }
    }
}

static void
parse_sprite_ref_name(char const* sprite_name, char* out_name, size_t out_name_size)
{
    if( !out_name || out_name_size == 0 )
        return;
    out_name[0] = '\0';
    if( !sprite_name || !sprite_name[0] )
        return;
    if( sscanf(sprite_name, "%63[^[]", out_name) < 1 )
        strncpy(out_name, sprite_name, out_name_size - 1);
    out_name[out_name_size - 1] = '\0';
}

struct RevConfigUISpriteRef const*
revconfig_ui_build_find_ref(
    struct RevConfigUIBuildState const* state,
    char const* sprite_name)
{
    if( !state || !sprite_name || !sprite_name[0] )
        return NULL;

    char name_buf[64];
    parse_sprite_ref_name(sprite_name, name_buf, sizeof(name_buf));

    for( int i = 0; i < state->sprite_ref_count; i++ )
    {
        if( strcmp(state->sprite_refs[i].name, name_buf) == 0 )
            return &state->sprite_refs[i];
    }
    return NULL;
}

int
revconfig_ui_build_lookup_sprite_id(
    struct RevConfigUIBuildState const* state,
    char const* sprite_name,
    int* out_atlas_index)
{
    if( out_atlas_index )
        *out_atlas_index = 0;
    if( !state || !sprite_name || !sprite_name[0] )
        return -1;

    char name_buf[64];
    int atlas_index = 0;
    if( sscanf(sprite_name, "%63[^[][%d", name_buf, &atlas_index) < 1 )
        strncpy(name_buf, sprite_name, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';

    for( int i = 0; i < state->sprite_ref_count; i++ )
    {
        if( strcmp(state->sprite_refs[i].name, name_buf) == 0 )
        {
            if( out_atlas_index )
                *out_atlas_index = atlas_index;
            return state->sprite_refs[i].element_id;
        }
    }
    return -1;
}

static struct RevConfigUIComponentItem const*
find_component(
    struct RevConfigUIBuildState const* state,
    char const* name)
{
    for( int i = 0; i < state->component_count; i++ )
    {
        if( strcmp(state->components[i].name, name) == 0 )
            return &state->components[i];
    }
    return NULL;
}

static int
component_id_from_item(struct RevConfigUIComponentItem const* comp)
{
    return comp && comp->componentno >= 0 ? comp->componentno : -1;
}

int32_t
revconfig_ui_build_node(
    struct RevConfigUIBuildState const* state,
    struct UITree* tree,
    struct RevConfigUILayoutItem const* le)
{
    if( !state || !tree || !le || le->component[0] == '\0' )
        return -1;

    struct RevConfigUIComponentItem const* comp = find_component(state, le->component);
    if( !comp )
        return -1;

    int sprite_id = -1;
    int atlas_index = 0;
    int sprite_active_id = -1;
    int atlas_active_index = 0;
    if( comp->sprite[0] != '\0' )
        sprite_id = revconfig_ui_build_lookup_sprite_id(state, comp->sprite, &atlas_index);
    if( comp->sprite_active[0] != '\0' )
        sprite_active_id =
            revconfig_ui_build_lookup_sprite_id(state, comp->sprite_active, &atlas_active_index);

    enum StaticUIComponentType ty = component_type_from_string(comp->type);
    int32_t idx = -1;
    int const component_id = component_id_from_item(comp);

    switch( ty )
    {
    case UIELEM_BUILTIN_COMPASS:
        idx = uitree_push_compass(
            tree,
            -1,
            sprite_id,
            atlas_index,
            le->x,
            le->y,
            comp->width,
            comp->height,
            comp->anchor_x,
            comp->anchor_y);
        break;
    case UIELEM_BUILTIN_MINIMAP:
        idx = uitree_push_minimap(
            tree,
            -1,
            le->x,
            le->y,
            comp->width,
            comp->height,
            comp->anchor_x,
            comp->anchor_y);
        break;
    case UIELEM_BUILTIN_WORLD:
        idx = uitree_push_world(
            tree,
            -1,
            le->x,
            le->y,
            comp->width,
            comp->height,
            parse_paint_levels_mask(comp->paint_levels));
        break;
    case UIELEM_BUILTIN_REDSTONE_TAB:
        idx = uitree_push_redstone_tab(
            tree,
            -1,
            comp->tabno,
            sprite_id,
            atlas_index,
            sprite_active_id,
            atlas_active_index,
            le->x,
            le->y,
            comp->width,
            comp->height);
        break;
    case UIELEM_BUILTIN_SIDEBAR:
        idx = uitree_push_builtin_sidebar(
            tree,
            -1,
            comp->tabno,
            comp->componentno,
            -1,
            le->x,
            le->y,
            comp->width,
            comp->height);
        break;
    case UIELEM_RS_GRAPHIC:
        idx = uitree_push_rs_graphic(
            tree,
            -1,
            component_id,
            sprite_id,
            atlas_index,
            sprite_active_id,
            atlas_active_index,
            le->x,
            le->y,
            comp->width,
            comp->height);
        break;
    case UIELEM_RS_LAYER:
        idx = uitree_push_rs_layer(
            tree, -1, component_id, le->x, le->y, comp->width, comp->height);
        break;
    case UIELEM_RS_TEXT:
    {
        int font_id = comp->font;
        if( font_id < 0 || font_id > 3 )
            font_id = 1;
        idx = uitree_push_rs_text(
            tree,
            -1,
            component_id,
            font_id,
            comp->color,
            comp->center ? 1 : 0,
            comp->shadowed ? 1 : 0,
            comp->text[0] != '\0' ? comp->text : NULL,
            le->x,
            le->y,
            comp->width,
            comp->height);
    }
    break;
    case UIELEM_RS_RECT:
        idx = uitree_push_rs_rect(
            tree,
            -1,
            component_id,
            comp->color,
            comp->filled ? 1 : 0,
            le->x,
            le->y,
            comp->width,
            comp->height);
        break;
    case UIELEM_BUILTIN_SPRITE:
    default:
        idx = uitree_push_sprite_xy(
            tree, -1, sprite_id, atlas_index, le->x, le->y, comp->width, comp->height);
        break;
    }

    if( idx >= 0 && le->dirty )
        tree->components[idx].always_dirty = 1;

    return idx;
}

bool
revconfig_ui_build_tree(
    struct RevConfigUIBuildState const* state,
    struct UITree* tree)
{
    if( !state || !tree || state->layout_entry_count <= 0 )
        return false;

    for( int i = 0; i < state->layout_entry_count; i++ )
        revconfig_ui_build_node(state, tree, &state->layout_entries[i]);

    uitree_mark_all_dirty(tree);
    return tree->component_count > 0;
}
