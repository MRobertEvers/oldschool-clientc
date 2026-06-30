#include "revconfig_ui_build.h"

#include "revconfig_ui_expand.h"
#include "toridraw/toridraw_sprite.h"
#include "ui/uitree_layout.h"

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
    if( strcmp(type, "builtin_tab_icons") == 0 || strcmp(type, "tab_icon") == 0 )
        return UIELEM_BUILTIN_TAB_ICONS;
    if( strcmp(type, "rs_model") == 0 )
        return UIELEM_RS_MODEL;
    if( strcmp(type, "rs_inv") == 0 )
        return UIELEM_RS_INV;
    if( strcmp(type, "rs_line") == 0 )
        return UIELEM_RS_LINE;
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
    for( int i = 0; i < (int)(sizeof(state->panel_root_id) / sizeof(state->panel_root_id[0])); i++ )
        state->panel_root_id[i] = -1;
    for( int i = 0;
         i < (int)(sizeof(state->layout_node_index) / sizeof(state->layout_node_index[0]));
         i++ )
        state->layout_node_index[i] = -1;
}

static int32_t
layout_parent_index(
    struct RevConfigUIBuildState const* state,
    char const* parent_name)
{
    if( !state || !parent_name || parent_name[0] == '\0' )
        return -1;

    for( int i = 0; i < state->layout_entry_count; i++ )
    {
        if( strcmp(state->layout_entries[i].name, parent_name) == 0 )
            return state->layout_node_index[i];
    }
    return -1;
}

static void
apply_layout_position(
    struct RevConfigUILayoutItem const* le,
    struct StaticUIElemPosition* pos,
    int comp_w,
    int comp_h)
{
    if( le->left || le->right || le->top || le->bottom )
    {
        pos->kind = UIPOS_RELATIVE;
        pos->relative_flags = 0;
        if( le->left )
            pos->relative_flags |= STATIC_UI_RELATIVE_FLAG_LEFT;
        if( le->right )
            pos->relative_flags |= STATIC_UI_RELATIVE_FLAG_RIGHT;
        if( le->top )
            pos->relative_flags |= STATIC_UI_RELATIVE_FLAG_TOP;
        if( le->bottom )
            pos->relative_flags |= STATIC_UI_RELATIVE_FLAG_BOTTOM;
        pos->left = le->left;
        pos->right = le->right;
        pos->top = le->top;
        pos->bottom = le->bottom;
        pos->width = le->width > 0 ? le->width : comp_w;
        pos->height = le->height > 0 ? le->height : comp_h;
    }
    else
    {
        pos->kind = UIPOS_XY;
        pos->x = le->x;
        pos->y = le->y;
        pos->width = le->width > 0 ? le->width : comp_w;
        pos->height = le->height > 0 ? le->height : comp_h;
    }
    if( le->has_anchor )
    {
        pos->anchor_x = le->anchor_x;
        pos->anchor_y = le->anchor_y;
    }
}

void
revconfig_ui_build_set_expand_context(
    struct RevConfigUIBuildState* state,
    struct ToriAuxLibCache* cache,
    struct Dat1BuildCache* bc,
    struct ToriDraw_Scene* scene,
    struct UIInventoryPool* inv_pool)
{
    if( !state )
        return;
    state->cache = cache;
    state->bc = bc;
    state->scene = scene;
    state->inv_pool = inv_pool;
}

void
revconfig_ui_build_populate_inv_pool(
    struct RevConfigUIBuildState* state,
    struct RevConfigItemBuffer const* items)
{
    if( !state || !state->inv_pool || !items )
        return;

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        if( item->kind != RCITEM_INV )
            continue;

        struct UIInventory inv = { 0 };
        strncpy(inv.name, item->u.inv.name, sizeof(inv.name) - 1);
        for( int j = 0; j < item->u.inv.item_count && inv.item_count < UI_INVENTORY_MAX_ITEMS; j++ )
        {
            inv.items[inv.item_count].obj_id = atoi(item->u.inv.items[j]);
            inv.items[inv.item_count].scene_id = -1;
            inv.items[inv.item_count].atlas_index = 0;
            inv.item_count++;
        }
        uitree_inv_pool_append(state->inv_pool, &inv);
    }
}

void
revconfig_ui_build_set_core(
    struct RevConfigUIBuildState* state,
    struct ToriAuxLibCore* core)
{
    if( !state )
        return;
    state->core = core;
}

static void
revconfig_ui_build_behavior_from_component(
    struct StaticUIBehavior* out,
    struct ToriAuxLibCore_Component const* comp)
{
    if( !out || !comp )
        return;

    memset(out, 0, sizeof(*out));
    out->hide = comp->hide;
    out->button_type = comp->button_type;
    out->client_code = comp->client_code;
    out->over_color = comp->over_color;
    out->active_color = comp->active_color;
    out->active_over_color = comp->active_over_color;
    out->scripts_count = comp->scripts_count;
    out->scripts = comp->scripts;
    out->scripts_lengths = comp->scripts_lengths;
    out->script_comparator = comp->script_comparator;
    out->script_operand = comp->script_operand;
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
        case RCITEM_CACHE_SPRITE:
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
            if( state->layout_entry_count < 192 )
                state->layout_entries[state->layout_entry_count++] = item->u.uilayout;
            break;
        case RCITEM_INV:
            if( state->inv_entry_count < REVCONFIG_UI_MAX_INV_ENTRIES )
                state->inv_entries[state->inv_entry_count++] = item->u.inv;
            break;
        default:
            break;
        }
    }
}

void
revconfig_ui_build_mark_needed_components(
    struct RevConfigUIBuildState const* state,
    bool* needed,
    int needed_count)
{
    if( !state || !needed || needed_count <= 0 )
        return;

    for( int i = 0; i < state->component_count; i++ )
    {
        int componentno = state->components[i].componentno;
        if( componentno >= 0 && componentno < needed_count )
            needed[componentno] = true;
    }
}

static void
parse_sprite_ref_name(
    char const* sprite_name,
    char* out_name,
    size_t out_name_size)
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

static bool
component_type_needs_behavior(enum StaticUIComponentType ty)
{
    switch( ty )
    {
    case UIELEM_RS_GRAPHIC:
    case UIELEM_RS_LAYER:
    case UIELEM_RS_TEXT:
    case UIELEM_RS_RECT:
    case UIELEM_RS_MODEL:
    case UIELEM_RS_INV:
    case UIELEM_RS_LINE:
        return true;
    default:
        return false;
    }
}

static void
revconfig_ui_build_infer_sprite_dims(
    struct RevConfigUIBuildState const* state,
    enum StaticUIComponentType ty,
    int scene_id,
    int atlas_index,
    int scene_active_id,
    int atlas_active_index,
    int* w,
    int* h)
{
    if( !state || !w || !h )
        return;
    if( *w > 0 && *h > 0 )
        return;

    if( ty != UIELEM_BUILTIN_TAB_ICONS && ty != UIELEM_BUILTIN_REDSTONE_TAB &&
        ty != UIELEM_BUILTIN_SPRITE )
        return;

    int lookup_id = scene_id;
    int lookup_atlas = atlas_index;
    if( ty == UIELEM_BUILTIN_REDSTONE_TAB && scene_active_id >= 0 )
    {
        lookup_id = scene_active_id;
        lookup_atlas = atlas_active_index;
    }
    if( lookup_id < 0 || !state->core )
        return;

    struct ToriAuxLibCore_Sprite* gc_sprite = ToriAuxLibCore_SpriteGet(state->core, lookup_id);
    if( !gc_sprite || !gc_sprite->sprites )
        return;
    if( lookup_atlas < 0 || lookup_atlas >= gc_sprite->count )
        return;

    struct ToriDraw_Sprite* sp = gc_sprite->sprites[lookup_atlas];
    if( !sp )
        return;

    int const sw = sp->crop_width > 0 ? sp->crop_width : sp->width;
    int const sh = sp->crop_height > 0 ? sp->crop_height : sp->height;
    if( *w <= 0 )
        *w = sw;
    if( *h <= 0 )
        *h = sh;
}

int32_t
revconfig_ui_build_node(
    struct RevConfigUIBuildState* state,
    struct UITree* tree,
    struct RevConfigUILayoutItem const* le,
    int32_t parent_index)
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
    int const component_id = component_id_from_item(comp);
    int w = le->width > 0 ? le->width : comp->width;
    int h = le->height > 0 ? le->height : comp->height;

    revconfig_ui_build_infer_sprite_dims(
        state, ty, sprite_id, atlas_index, sprite_active_id, atlas_active_index, &w, &h);

    struct UINodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = ty;
    spec.component_id = component_id;
    apply_layout_position(le, &spec.position, w, h);
    spec.has_position = 1;
    if( le->dirty )
        spec.always_dirty = 1;

    if( ty == UIELEM_BUILTIN_COMPASS || ty == UIELEM_BUILTIN_MINIMAP )
    {
        if( !le->has_anchor )
        {
            spec.position.anchor_x = comp->anchor_x;
            spec.position.anchor_y = comp->anchor_y;
        }
    }

    switch( ty )
    {
    case UIELEM_BUILTIN_COMPASS:
        spec.u.sprite.scene_id = sprite_id;
        spec.u.sprite.atlas_index = atlas_index;
        break;
    case UIELEM_BUILTIN_MINIMAP:
        break;
    case UIELEM_BUILTIN_WORLD:
        spec.u.world.level_mask = parse_paint_levels_mask(comp->paint_levels);
        break;
    case UIELEM_BUILTIN_REDSTONE_TAB:
        spec.u.redstone_tab.tabno = comp->tabno;
        spec.u.redstone_tab.scene_id = sprite_id;
        spec.u.redstone_tab.atlas_index = atlas_index;
        spec.u.redstone_tab.scene_id_active = sprite_active_id;
        spec.u.redstone_tab.atlas_index_active = atlas_active_index;
        break;
    case UIELEM_BUILTIN_TAB_ICONS:
        spec.u.tab_icon.tabno = comp->tabno;
        spec.u.tab_icon.scene_id = sprite_id;
        spec.u.tab_icon.atlas_index = atlas_index;
        break;
    case UIELEM_BUILTIN_SIDEBAR:
    {
        int inv_index = -1;
        if( comp->inv[0] != '\0' && state->inv_pool )
            inv_index = uitree_inv_pool_find_by_name(state->inv_pool, comp->inv);
        spec.u.sidebar.tabno = comp->tabno;
        spec.u.sidebar.componentno = comp->componentno;
        spec.u.sidebar.inv_index = inv_index;
        break;
    }
    case UIELEM_RS_GRAPHIC:
        spec.u.rs_graphic.scene_id = sprite_id;
        spec.u.rs_graphic.atlas_index = atlas_index;
        spec.u.rs_graphic.scene_id_active = sprite_active_id;
        spec.u.rs_graphic.atlas_index_active = atlas_active_index;
        break;
    case UIELEM_RS_LAYER:
        break;
    case UIELEM_RS_TEXT:
    {
        int font_id = comp->font;
        if( font_id < 0 || font_id > 3 )
            font_id = 1;
        spec.u.rs_text.font_id = font_id;
        spec.u.rs_text.color = comp->color;
        spec.u.rs_text.center = comp->center ? 1 : 0;
        spec.u.rs_text.shadowed = comp->shadowed ? 1 : 0;
        spec.u.rs_text.text = comp->text[0] != '\0' ? comp->text : NULL;
        break;
    }
    case UIELEM_RS_RECT:
        spec.u.rs_rect.color = comp->color;
        spec.u.rs_rect.filled = comp->filled ? 1 : 0;
        break;
    case UIELEM_RS_MODEL:
        spec.u.rs_model.gamecache_model_id = component_id;
        spec.u.rs_model.zoom = 100;
        spec.u.rs_model.xan = 0;
        spec.u.rs_model.yan = 0;
        break;
    case UIELEM_RS_INV:
        spec.u.rs_inv.inv_index = -1;
        spec.u.rs_inv.cols = 4;
        spec.u.rs_inv.rows = 7;
        spec.u.rs_inv.margin_x = 0;
        spec.u.rs_inv.margin_y = 0;
        break;
    case UIELEM_RS_LINE:
        spec.u.rs_line.color = comp->color;
        spec.u.rs_line.line_width = 1;
        spec.u.rs_line.horizontal = 1;
        break;
    case UIELEM_BUILTIN_SPRITE:
    default:
        spec.type = UIELEM_BUILTIN_SPRITE;
        spec.u.sprite.scene_id = sprite_id;
        spec.u.sprite.atlas_index = atlas_index;
        break;
    }

    struct StaticUIBehavior behavior;
    if( component_id >= 0 && state->core && component_type_needs_behavior(ty) )
    {
        struct ToriAuxLibCore_Component* gc_comp =
            ToriAuxLibCore_ComponentGet(state->core, component_id);
        if( gc_comp )
        {
            revconfig_ui_build_behavior_from_component(&behavior, gc_comp);
            spec.behavior = &behavior;
        }
    }

    int32_t idx = uitree_push(tree, parent_index, &spec);

    if( idx >= 0 && ty == UIELEM_BUILTIN_SIDEBAR && comp->componentno >= 0 && state->bc &&
        state->scene )
    {
        int inv_index = spec.u.sidebar.inv_index;
        revconfig_ui_expand_sidebar(state, tree, idx, comp->componentno, inv_index, le->x, le->y);
    }

    return idx;
}

bool
revconfig_ui_build_tree(
    struct RevConfigUIBuildState* state,
    struct UITree* tree,
    char const* active_layout_group)
{
    if( !state || !tree || state->layout_entry_count <= 0 )
        return false;

    for( int i = 0; i < state->layout_entry_count; i++ )
        state->layout_node_index[i] = -1;

    bool progress = true;
    int built = 0;
    int guard = 0;
    int active_count = 0;
    for( int i = 0; i < state->layout_entry_count; i++ )
    {
        struct RevConfigUILayoutItem const* le = &state->layout_entries[i];
        if( active_layout_group && active_layout_group[0] != '\0' && le->layout_group[0] != '\0' &&
            strcmp(le->layout_group, active_layout_group) != 0 )
            continue;
        active_count++;
    }
    if( active_count <= 0 )
        return false;

    while( built < active_count && progress && guard < active_count * 4 )
    {
        progress = false;
        guard++;

        for( int i = 0; i < state->layout_entry_count; i++ )
        {
            struct RevConfigUILayoutItem const* le = &state->layout_entries[i];
            if( active_layout_group && active_layout_group[0] != '\0' &&
                le->layout_group[0] != '\0' && strcmp(le->layout_group, active_layout_group) != 0 )
                continue;

            if( state->layout_node_index[i] >= 0 )
                continue;
            int32_t parent_index = layout_parent_index(state, le->parent);
            if( le->parent[0] != '\0' && parent_index < 0 )
                continue;

            int32_t idx = revconfig_ui_build_node(state, tree, le, parent_index);
            if( idx < 0 )
                continue;

            state->layout_node_index[i] = idx;
            built++;
            progress = true;
        }
    }

    uitree_layout_resolve(tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    uitree_mark_all_dirty(tree);
    return tree->component_count > 0;
}
