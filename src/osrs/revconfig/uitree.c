#include "uitree.h"

#include <stdlib.h>
#include <string.h>

static struct StaticUIComponent*
push_element(struct UITree* tree)
{
    if( tree->component_count >= tree->component_capacity )
    {
        if( tree->component_capacity == 0 )
            tree->component_capacity = 16;

        uint32_t new_capacity = tree->component_capacity == 0 ? 16 : tree->component_capacity * 2;
        struct StaticUIComponent* new_components =
            realloc(tree->components, new_capacity * sizeof(struct StaticUIComponent));
        if( !new_components )
            return NULL;
        tree->components = new_components;
        tree->component_capacity = new_capacity;
    }

    struct StaticUIComponent* component = &tree->components[tree->component_count++];
    return component;
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
    struct UITree* tree = malloc(sizeof(struct UITree));
    if( !tree )
        return NULL;
    memset(tree, 0, sizeof(struct UITree));
    return tree;
}

void
uitree_free(struct UITree* tree)
{
    if( !tree )
        return;
    free(tree->components);
    free(tree);
}

void
uitree_push_sprite_xy(
    struct UITree* tree,
    int sprite_id,
    int atlas_index,
    int x,
    int y,
    int width,
    int height)
{
    struct StaticUIComponent* component = push_element(tree);
    if( !component )
        return;

    component->type = UIELEM_BUILTIN_SPRITE;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.sprite.scene_id = sprite_id;
    component->u.sprite.atlas_index = atlas_index;
}

#define STATIC_UI_RELATIVE_FLAG_LEFT 1
#define STATIC_UI_RELATIVE_FLAG_TOP 2
#define STATIC_UI_RELATIVE_FLAG_RIGHT 4
#define STATIC_UI_RELATIVE_FLAG_BOTTOM 8

void
uitree_push_sprite_relative(
    struct UITree* tree,
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
    struct StaticUIComponent* component = push_element(tree);
    if( !component )
        return;

    memset(component, 0, sizeof(struct StaticUIComponent));

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
}

void
uitree_push_world(
    struct UITree* tree,
    int x,
    int y)
{
    struct StaticUIComponent* component = push_element(tree);
    if( !component )
        return;

    memset(component, 0, sizeof(struct StaticUIComponent));

    component->type = UIELEM_BUILTIN_WORLD;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
}

void
uitree_push_compass(
    struct UITree* tree,
    int sprite_id,
    int atlas_index,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y)
{
    struct StaticUIComponent* component = push_element(tree);
    if( !component )
        return;

    memset(component, 0, sizeof(struct StaticUIComponent));

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
}

void
uitree_push_minimap(
    struct UITree* tree,
    int x,
    int y,
    int width,
    int height,
    int anchor_x,
    int anchor_y)
{
    struct StaticUIComponent* component = push_element(tree);
    if( !component )
        return;

    memset(component, 0, sizeof(struct StaticUIComponent));

    component->type = UIELEM_BUILTIN_MINIMAP;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->position.anchor_x = anchor_x;
    component->position.anchor_y = anchor_y;
}

void
uitree_push_redstone_tab(
    struct UITree* tree,
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
    struct StaticUIComponent* component = push_element(tree);
    if( !component )
        return;
    memset(component, 0, sizeof(struct StaticUIComponent));
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
}

void
uitree_push_builtin_sidebar(
    struct UITree* tree,
    int tabno,
    int componentno,
    int x,
    int y,
    int width,
    int height)
{
    struct StaticUIComponent* component = push_element(tree);
    if( !component )
        return;
    memset(component, 0, sizeof(struct StaticUIComponent));
    component->type = UIELEM_BUILTIN_SIDEBAR;
    component->position.kind = UIPOS_XY;
    component->position.x = x;
    component->position.y = y;
    component->position.width = width;
    component->position.height = height;
    component->u.sidebar.tabno = tabno;
    component->u.sidebar.componentno = componentno;
}
