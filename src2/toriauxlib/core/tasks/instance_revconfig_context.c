#include "instance_revconfig_context.h"

#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat2_buildcache.h"
#include "games/runescape.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "ui/uitree_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct InstanceRevConfigRSSubtree*
instance_revconfig_rs_subtree_find(
    struct InstanceRevConfigContext* ctx,
    char const* owner_component)
{
    if( !ctx || !owner_component || owner_component[0] == '\0' )
        return NULL;
    for( int i = 0; i < ctx->rs_subtree_count; i++ )
    {
        if( strcmp(ctx->rs_subtrees[i].owner_component, owner_component) == 0 )
            return &ctx->rs_subtrees[i];
    }
    return NULL;
}

struct InstanceRevConfigRSSubtree*
instance_revconfig_rs_subtree_get_or_create(
    struct InstanceRevConfigContext* ctx,
    char const* owner_component)
{
    assert(
        ctx && owner_component && owner_component[0] != '\0' &&
        ctx->rs_subtree_count < INSTANCE_RC_MAX_RS_SUBTREES);
    struct InstanceRevConfigRSSubtree* existing =
        instance_revconfig_rs_subtree_find(ctx, owner_component);
    if( existing )
        return existing;

    struct InstanceRevConfigRSSubtree* subtree = &ctx->rs_subtrees[ctx->rs_subtree_count++];
    memset(subtree, 0, sizeof(*subtree));
    strncpy(subtree->owner_component, owner_component, sizeof(subtree->owner_component) - 1);
    return subtree;
}

bool
instance_revconfig_rs_subtree_append(
    struct InstanceRevConfigRSSubtree* subtree,
    int component_id)
{
    assert(subtree && component_id >= 0 && subtree->item_count < INSTANCE_RC_MAX_RS_SUBTREE_ITEMS);
    subtree->component_ids[subtree->item_count++] = component_id;
    return true;
}

static int32_t
instance_revconfig_bake_rs_component(
    struct InstanceRevConfigContext* ctx,
    int32_t parent_idx,
    struct ToriAuxLibCore_Component const* info,
    int inv_index)
{
    assert(ctx && ctx->tree && info);

    struct StaticUIBehavior behavior;
    struct StaticUIBehavior* behavior_ptr = NULL;
    memset(&behavior, 0, sizeof(behavior));
    behavior.hide = info->hide;
    behavior.button_type = info->button_type;
    behavior.client_code = info->client_code;
    if( info->scripts_count > 0 )
    {
        behavior.over_color = info->over_color;
        behavior.active_color = info->active_color;
        behavior.active_over_color = info->active_over_color;
        behavior.scripts_count = info->scripts_count;
        behavior.scripts = info->scripts;
        behavior.scripts_lengths = info->scripts_lengths;
        behavior.script_comparator = info->script_comparator;
        behavior.script_operand = info->script_operand;
    }
    behavior_ptr = &behavior;

    struct UINodeSpec spec = { 0 };
    spec.component_id = info->id;
    spec.x = info->rel_x;
    spec.y = info->rel_y;
    spec.width = info->width;
    spec.height = info->height;
    spec.behavior = behavior_ptr;

    switch( info->type )
    {
    case TORIAUXLIBCORE_COMPONENT_LAYER:
        spec.type = UIELEM_RS_LAYER;
        break;
    case TORIAUXLIBCORE_COMPONENT_GRAPHIC:
    {
        int atlas = 0;
        int atlas_a = 0;
        int sid = ui_sprite_lookup_resolve_ref(&ctx->sprite_lookup, info->sprite_ref, &atlas);
        int sid_a =
            ui_sprite_lookup_resolve_ref(&ctx->sprite_lookup, info->sprite_active_ref, &atlas_a);
        if( sid < 0 && sid_a < 0 )
            return -1;
        spec.type = UIELEM_RS_GRAPHIC;
        spec.u.rs_graphic.scene_id = sid >= 0 ? sid : sid_a;
        spec.u.rs_graphic.atlas_index = sid >= 0 ? atlas : atlas_a;
        spec.u.rs_graphic.scene_id_active = sid >= 0 && sid_a >= 0 && sid_a != sid ? sid_a : -1;
        spec.u.rs_graphic.atlas_index_active = sid_a >= 0 ? atlas_a : 0;
        break;
    }
    case TORIAUXLIBCORE_COMPONENT_RECT:
        spec.type = UIELEM_RS_RECT;
        spec.u.rs_rect.color = info->color;
        spec.u.rs_rect.filled = info->filled;
        break;
    case TORIAUXLIBCORE_COMPONENT_TEXT:
    case TORIAUXLIBCORE_COMPONENT_INV_TEXT:
        spec.type = UIELEM_RS_TEXT;
        spec.u.rs_text.font_id = info->font_id >= 0 && info->font_id <= 3 ? info->font_id : 1;
        spec.u.rs_text.color = info->color;
        spec.u.rs_text.center = info->center;
        spec.u.rs_text.shadowed = info->shadowed;
        spec.u.rs_text.text = info->text[0] != '\0' ? info->text : NULL;
        break;
    case TORIAUXLIBCORE_COMPONENT_MODEL:
        spec.type = UIELEM_RS_MODEL;
        spec.u.rs_model.gamecache_model_id = info->model_id;
        spec.u.rs_model.zoom = 100;
        break;
    case TORIAUXLIBCORE_COMPONENT_INV:
        spec.type = UIELEM_RS_INV;
        spec.u.rs_inv.inv_index = inv_index;
        spec.u.rs_inv.cols = info->inv_cols;
        spec.u.rs_inv.rows = info->inv_rows;
        spec.u.rs_inv.margin_x = info->margin_x;
        spec.u.rs_inv.margin_y = info->margin_y;
        break;
    default:
        return -1;
    }

    return uitree_push(ctx->tree, parent_idx, &spec);
}

static int
instance_revconfig_rs_file_index(int component_id)
{
    return component_id & 0xFFFF;
}

void
instance_revconfig_bake_rs_subtree(
    struct InstanceRevConfigContext* ctx,
    struct RevConfigUIComponentItem const* comp,
    int32_t owner_uitree_index)
{
    assert(ctx && comp && owner_uitree_index >= 0);

    struct InstanceRevConfigRSSubtree* subtree =
        instance_revconfig_rs_subtree_find(ctx, comp->name);
    assert(subtree && subtree->item_count > 0);

    int inv_index = -1;
    if( comp->inv[0] != '\0' && ctx->inv_pool )
        inv_index = uitree_inv_pool_find_by_name(ctx->inv_pool, comp->inv);

    int id_to_uitree[INSTANCE_RC_RS_FILE_INDEX_MAX];
    for( int i = 0; i < INSTANCE_RC_RS_FILE_INDEX_MAX; i++ )
        id_to_uitree[i] = -1;

    for( int i = 0; i < subtree->item_count; i++ )
    {
        int component_id = subtree->component_ids[i];
        struct ToriAuxLibCore_Component* info =
            ctx->core ? ToriAuxLibCore_ComponentGet(ctx->core, component_id) : NULL;
        if( !info )
            continue;

        int32_t parent_idx = owner_uitree_index;
        if( info->parent_id >= 0 )
        {
            int parent_file = instance_revconfig_rs_file_index(info->parent_id);
            if( parent_file < 0 || parent_file >= INSTANCE_RC_RS_FILE_INDEX_MAX ||
                id_to_uitree[parent_file] < 0 )
                continue;
            parent_idx = id_to_uitree[parent_file];
        }

        int32_t idx = instance_revconfig_bake_rs_component(ctx, parent_idx, info, inv_index);
        if( idx < 0 )
            continue;

        int file_idx = instance_revconfig_rs_file_index(info->id);
        if( file_idx >= 0 && file_idx < INSTANCE_RC_RS_FILE_INDEX_MAX )
            id_to_uitree[file_idx] = idx;
    }
}

void
instance_revconfig_context_release_build_state(struct InstanceRevConfigContext* ctx)
{
    assert(ctx);
    if( ctx->dat1_bc )
        dat1_buildcache_set_interfaces(ctx->dat1_bc, NULL);
    if( ctx->dat2_bc )
        dat2_buildcache_interfaces_cleanup(ctx->dat2_bc);
}

void
instance_revconfig_context_init(struct InstanceRevConfigContext* ctx)
{
    assert(ctx);
    memset(ctx, 0, sizeof(*ctx));
    ui_sprite_lookup_init(&ctx->sprite_lookup);
    ctx->next_element_id = 1;
    ctx->layout_group = "fixed";
    for( int i = 0; i < 1024; i++ )
        ctx->panel_root_id[i] = -1;
    for( int i = 0; i < INSTANCE_RC_MAX_LAYOUTS; i++ )
        ctx->layout_node_index[i] = -1;
}

struct RevConfigUIComponentItem const*
instance_revconfig_find_component(
    struct InstanceRevConfigContext const* ctx,
    char const* name)
{
    assert(ctx && name);
    for( int i = 0; i < ctx->component_count; i++ )
    {
        if( strcmp(ctx->components[i].name, name) == 0 )
            return &ctx->components[i];
    }
    return NULL;
}

int32_t
instance_revconfig_layout_parent_index(
    struct InstanceRevConfigContext const* ctx,
    char const* parent_name)
{
    assert(ctx && parent_name);
    if( parent_name[0] == '\0' )
        return -1;

    for( int i = 0; i < ctx->layout_count; i++ )
    {
        if( strcmp(ctx->layouts[i].name, parent_name) == 0 )
            return ctx->layout_node_index[i];
    }
    return -1;
}

static enum StaticUIComponentType
component_type_from_string(char const* type)
{
    assert(type);
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
    assert(str);
    if( str[0] == '\0' )
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

static void
apply_layout_position(
    struct RevConfigUILayoutItem const* le,
    struct RevConfigUIComponentItem const* comp,
    struct StaticUIElemPosition* pos)
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
        pos->width = le->width > 0 ? le->width : comp->width;
        pos->height = le->height > 0 ? le->height : comp->height;
    }
    else
    {
        pos->kind = UIPOS_XY;
        pos->x = le->x;
        pos->y = le->y;
        pos->width = le->width > 0 ? le->width : comp->width;
        pos->height = le->height > 0 ? le->height : comp->height;
    }
    if( le->has_anchor )
    {
        pos->anchor_x = le->anchor_x;
        pos->anchor_y = le->anchor_y;
    }
    else
    {
        pos->anchor_x = comp->anchor_x;
        pos->anchor_y = comp->anchor_y;
    }
}

static int
component_id_from_item(struct RevConfigUIComponentItem const* comp)
{
    assert(comp);
    if( comp->componentno < 0 )
        return -1;
    if( comp->componentno >= 0 )
        return comp->componentno;
    return -1;
}

void
instance_revconfig_resolve_panel_roots(struct InstanceRevConfigContext* ctx)
{
    assert(ctx && ctx->dat1_bc);

    struct RSCacheDat1A_ConfigComponentList* ifaces = dat1_buildcache_get_interfaces(ctx->dat1_bc);
    assert(ifaces);

    for( int i = 0; i < ifaces->components_count; i++ )
    {
        struct RSCacheDat1A_ConfigComponent* comp = ifaces->components[i];
        if( !comp || comp->type != COMPONENT_TYPE_INV || comp->width != 4 || comp->height != 7 )
            continue;
        if( comp->layer < 0 || comp->layer >= 1024 )
            continue;
        if( ctx->panel_root_id[149] < 0 )
            ctx->panel_root_id[149] = comp->layer;
    }

    for( int i = 0; i < ctx->component_count; i++ )
    {
        struct RevConfigUIComponentItem const* sidebar = &ctx->components[i];
        if( strcmp(sidebar->type, "sidebar") != 0 || sidebar->componentno < 0 ||
            sidebar->componentno >= 1024 )
            continue;
        if( ctx->panel_root_id[sidebar->componentno] >= 0 )
            continue;

        struct RSCacheDat1A_ConfigComponent* direct = NULL;
        if( sidebar->componentno < ifaces->components_count )
            direct = ifaces->components[sidebar->componentno];
        if( direct && direct->type == COMPONENT_TYPE_LAYER && direct->children_count > 0 )
            ctx->panel_root_id[sidebar->componentno] = direct->id;
    }
}

static int32_t
instance_revconfig_build_layout_node(
    struct InstanceRevConfigContext* ctx,
    struct RevConfigUILayoutItem const* layout,
    int32_t parent_index)
{
    assert(ctx && ctx->tree && layout && layout->component[0] != '\0');
    struct RevConfigUIComponentItem const* comp =
        instance_revconfig_find_component(ctx, layout->component);
    assert(comp);

    int sprite_id = -1;
    int atlas_index = 0;
    int sprite_active_id = -1;
    int atlas_active_index = 0;

    if( comp->sprite[0] != '\0' )
        sprite_id = ui_sprite_lookup_resolve_ref(&ctx->sprite_lookup, comp->sprite, &atlas_index);

    if( comp->sprite_active[0] != '\0' )
        sprite_active_id = ui_sprite_lookup_resolve_ref(
            &ctx->sprite_lookup, comp->sprite_active, &atlas_active_index);

    enum StaticUIComponentType static_type = component_type_from_string(comp->type);
    int const component_id = component_id_from_item(comp);

    struct UINodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = static_type;
    spec.component_id = component_id;
    apply_layout_position(layout, comp, &spec.position);
    spec.has_position = 1;
    if( layout->dirty )
        spec.always_dirty = 1;

    switch( static_type )
    {
    case UIELEM_BUILTIN_COMPASS:
        spec.u.sprite.scene_id = sprite_id;
        spec.u.sprite.atlas_index = atlas_index;
        break;
    case UIELEM_BUILTIN_MINIMAP:
        spec.always_dirty = 1;
        spec.u.minimap.scene_id = sprite_id;
        break;
    case UIELEM_BUILTIN_CHAT:
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
        if( comp->inv[0] != '\0' && ctx->inv_pool )
            inv_index = uitree_inv_pool_find_by_name(ctx->inv_pool, comp->inv);
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
    case UIELEM_RS_LAYER:
        spec.u.rs_layer.reserved = 0;
        break;
    case UIELEM_RS_MODEL:
        spec.u.rs_model.gamecache_model_id = component_id >= 0 ? component_id : 0;
        spec.u.rs_model.zoom = 100;
        spec.u.rs_model.xan = 0;
        spec.u.rs_model.yan = 0;
        break;
    case UIELEM_RS_INV:
    {
        int inv_index = -1;
        if( comp->inv[0] != '\0' && ctx->inv_pool )
            inv_index = uitree_inv_pool_find_by_name(ctx->inv_pool, comp->inv);
        spec.u.rs_inv.inv_index = inv_index;
        spec.u.rs_inv.cols = comp->width > 0 ? comp->width : 4;
        spec.u.rs_inv.rows = comp->height > 0 ? comp->height : 7;
        spec.u.rs_inv.margin_x = 0;
        spec.u.rs_inv.margin_y = 0;
        break;
    }
    case UIELEM_RS_LINE:
        spec.u.rs_line.color = comp->color;
        spec.u.rs_line.line_width = 1;
        spec.u.rs_line.horizontal = comp->filled ? 1 : 0;
        break;
    case UIELEM_BUILTIN_SPRITE:
        spec.u.sprite.scene_id = sprite_id;
        spec.u.sprite.atlas_index = atlas_index;
        break;
    default:
        break;
    }

    int32_t idx = uitree_push(ctx->tree, parent_index, &spec);
    assert(idx >= 0);
    if( comp->componentno >= 0 )
        instance_revconfig_bake_rs_subtree(ctx, comp, idx);

    return idx;
}

bool
instance_revconfig_build_tree(struct InstanceRevConfigContext* ctx)
{
    assert(ctx && ctx->tree);

    for( int i = 0; i < ctx->layout_count; i++ )
        ctx->layout_node_index[i] = -1;

    char const* active_layout_group = ctx->layout_group;
    bool progress = true;
    int built = 0;
    int guard = 0;
    int active_count = 0;

    for( int i = 0; i < ctx->layout_count; i++ )
    {
        struct RevConfigUILayoutItem const* le = &ctx->layouts[i];
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

        for( int i = 0; i < ctx->layout_count; i++ )
        {
            struct RevConfigUILayoutItem const* le = &ctx->layouts[i];
            if( active_layout_group && active_layout_group[0] != '\0' &&
                le->layout_group[0] != '\0' && strcmp(le->layout_group, active_layout_group) != 0 )
                continue;

            if( ctx->layout_node_index[i] >= 0 )
                continue;

            int32_t parent_index = instance_revconfig_layout_parent_index(ctx, le->parent);
            if( le->parent[0] != '\0' && parent_index < 0 )
                continue;

            int32_t idx = instance_revconfig_build_layout_node(ctx, le, parent_index);
            assert(idx >= 0);

            ctx->layout_node_index[i] = idx;
            built++;
            progress = true;
        }
    }

    uitree_layout_resolve(ctx->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    uitree_mark_all_dirty(ctx->tree);

    return true;
}
