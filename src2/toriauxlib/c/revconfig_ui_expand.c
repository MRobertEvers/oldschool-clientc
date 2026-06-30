#include "revconfig_ui_expand.h"

#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat1_buildcache_ui.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toridraw/toridraw_scene.h"
#include "toridraw/toridraw_sprite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct RSCacheDat1A_ConfigComponent*
expand_get_iface_component(
    struct RSCacheDat1A_ConfigComponentList* interfaces,
    int component_id)
{
    if( !interfaces || component_id < 0 )
        return NULL;

    if( component_id < interfaces->components_count && interfaces->components[component_id] )
        return interfaces->components[component_id];

    for( int i = 0; i < interfaces->components_count; i++ )
    {
        struct RSCacheDat1A_ConfigComponent* comp = interfaces->components[i];
        if( comp && comp->id == component_id )
            return comp;
    }
    return NULL;
}

static void
expand_apply_behavior(
    struct RevConfigUIBuildState* state,
    struct UITree* tree,
    int32_t idx,
    int component_id)
{
    if( !state || !tree || idx < 0 || component_id < 0 || !state->core )
        return;

    struct ToriAuxLibCore_Component* gc_comp =
        ToriAuxLibCore_ComponentGet(state->core, component_id);
    if( !gc_comp )
        return;

    struct StaticUIBehavior behavior;
    memset(&behavior, 0, sizeof(behavior));
    /* Sidebar bake: do not copy interface hide; tab visibility is via sidebar parent. */
    behavior.hide = 0;
    behavior.button_type = gc_comp->button_type;
    behavior.client_code = gc_comp->client_code;
    behavior.over_color = gc_comp->over_color;
    behavior.active_color = gc_comp->active_color;
    behavior.active_over_color = gc_comp->active_over_color;
    behavior.scripts_count = gc_comp->scripts_count;
    behavior.scripts = gc_comp->scripts;
    behavior.scripts_lengths = gc_comp->scripts_lengths;
    behavior.script_comparator = gc_comp->script_comparator;
    behavior.script_operand = gc_comp->script_operand;
    uitree_set_behavior(tree, idx, &behavior);
}

static int
expand_acquire_sprite(
    struct RevConfigUIBuildState* state,
    char const* sprite_ref)
{
    if( !state || !sprite_ref || !sprite_ref[0] || !state->scene || !state->bc )
        return -1;

    for( int i = 0; i < state->dynamic_sprite_count; i++ )
    {
        if( strcmp(state->dynamic_sprites[i].name, sprite_ref) == 0 )
            return state->dynamic_sprites[i].element_id;
    }

    struct ToriDraw_Sprite* sprite = dat1_buildcache_sprite_decode_ref(state->bc, sprite_ref);
    if( !sprite )
        return -1;

    int element_id = state->next_element_id++;
    struct ToriDraw_Sprite** row = malloc(sizeof(struct ToriDraw_Sprite*));
    if( !row )
    {
        ToriDraw_SpriteFree(sprite);
        return -1;
    }
    row[0] = sprite;
    ToriDraw_SceneSpriteAdd(state->scene, element_id, row, 1);

    if( state->cache )
    {
        struct ToriAuxLibCore_Sprite* gc_sprite = calloc(1, sizeof(struct ToriAuxLibCore_Sprite));
        if( gc_sprite )
        {
            strncpy(gc_sprite->name, sprite_ref, sizeof(gc_sprite->name) - 1);
            gc_sprite->sprites = row;
            gc_sprite->count = 1;
            ToriAuxLibCache_SubmitSpriteFromDat1(state->cache, element_id, gc_sprite);
        }
    }

    if( state->dynamic_sprite_count < REVCONFIG_UI_DYNAMIC_SPRITE_MAX )
    {
        struct RevConfigUIDynamicSprite* slot =
            &state->dynamic_sprites[state->dynamic_sprite_count++];
        strncpy(slot->name, sprite_ref, sizeof(slot->name) - 1);
        slot->element_id = element_id;
    }

    return element_id;
}

static struct RSCacheDat1A_ConfigComponent*
expand_resolve_sidebar_root(
    struct RevConfigUIBuildState* state,
    struct RSCacheDat1A_ConfigComponentList* interfaces,
    int panel_id,
    int inv_index)
{
    int root_id = panel_id;
    if( state && panel_id >= 0 && panel_id < 1024 && state->panel_root_id[panel_id] >= 0 )
        root_id = state->panel_root_id[panel_id];

    struct RSCacheDat1A_ConfigComponent* root = expand_get_iface_component(interfaces, root_id);
    if( root && root->type == COMPONENT_TYPE_LAYER && root->children_count > 0 )
        return root;

    if( inv_index >= 0 && interfaces )
    {
        for( int i = 0; i < interfaces->components_count; i++ )
        {
            struct RSCacheDat1A_ConfigComponent* comp = interfaces->components[i];
            if( !comp || comp->type != COMPONENT_TYPE_INV || comp->width != 4 || comp->height != 7 )
                continue;
            struct RSCacheDat1A_ConfigComponent* layer_root =
                expand_get_iface_component(interfaces, comp->layer);
            if( layer_root && layer_root->type == COMPONENT_TYPE_LAYER )
                return layer_root;
        }
    }

    if( root && root->type == COMPONENT_TYPE_LAYER )
        return root;

    return root;
}

static void
expand_bake_rs_subtree(
    struct RevConfigUIBuildState* state,
    struct UITree* tree,
    struct RSCacheDat1A_ConfigComponentList* interfaces,
    int32_t parent_idx,
    struct RSCacheDat1A_ConfigComponent* comp,
    int rel_x,
    int rel_y,
    int sidebar_inv_index)
{
    int32_t layer_idx;
    int sid;
    int sid_a;
    int fid;
    int bg_sid[UI_INV_SLOT_OFFSET_MAX];
    int bg_ai[UI_INV_SLOT_OFFSET_MAX];
    int32_t idx;

    if( !state || !tree || !interfaces || !comp )
        return;

    switch( comp->type )
    {
    case COMPONENT_TYPE_LAYER:
        layer_idx = uitree_push_rs_layer(
            tree, parent_idx, comp->id, rel_x, rel_y, comp->width, comp->height);
        if( layer_idx < 0 )
            return;
        expand_apply_behavior(state, tree, layer_idx, comp->id);
        if( !comp->children )
            return;

        for( int i = 0; i < comp->children_count; i++ )
        {
            struct RSCacheDat1A_ConfigComponent* child =
                expand_get_iface_component(interfaces, comp->children[i]);
            int cx;
            int cy;

            if( !child )
                continue;

            cx = (comp->childX ? comp->childX[i] : 0) + child->x;
            cy = (comp->childY ? comp->childY[i] : 0) + child->y;
            expand_bake_rs_subtree(
                state, tree, interfaces, layer_idx, child, cx, cy, sidebar_inv_index);
        }
        break;

    case COMPONENT_TYPE_GRAPHIC:
        sid = expand_acquire_sprite(state, comp->graphic);
        sid_a = expand_acquire_sprite(state, comp->activeGraphic);
        if( sid < 0 && sid_a < 0 )
            return;

        idx = uitree_push_rs_graphic(
            tree,
            parent_idx,
            comp->id,
            sid >= 0 ? sid : sid_a,
            0,
            sid >= 0 && sid_a >= 0 && sid_a != sid ? sid_a : -1,
            0,
            rel_x,
            rel_y,
            comp->width,
            comp->height);
        if( idx >= 0 )
            expand_apply_behavior(state, tree, idx, comp->id);
        break;

    case COMPONENT_TYPE_RECT:
        if( comp->fill )
        {
            idx = uitree_push_rs_rect(
                tree,
                parent_idx,
                comp->id,
                comp->colour,
                comp->fill ? 1 : 0,
                rel_x,
                rel_y,
                comp->width,
                comp->height);
            if( idx >= 0 )
                expand_apply_behavior(state, tree, idx, comp->id);
        }
        break;

    case COMPONENT_TYPE_TEXT:
    case COMPONENT_TYPE_INV_TEXT:
        fid = comp->font;
        if( fid < 0 || fid > 3 )
            fid = 1;
        idx = uitree_push_rs_text(
            tree,
            parent_idx,
            comp->id,
            fid,
            comp->colour,
            comp->center ? 1 : 0,
            comp->shadowed ? 1 : 0,
            comp->text,
            rel_x,
            rel_y,
            comp->width,
            comp->height);
        if( idx >= 0 )
            expand_apply_behavior(state, tree, idx, comp->id);
        break;

    case COMPONENT_TYPE_MODEL:
        if( comp->modelType == 1 && state->core )
        {
            struct ToriAuxLibCore_Model* model = ToriAuxLibCore_ModelGet(state->core, comp->model);
            if( model )
            {
                idx = uitree_push_rs_model(
                    tree,
                    parent_idx,
                    comp->id,
                    comp->model,
                    comp->zoom,
                    comp->xan,
                    comp->yan,
                    rel_x,
                    rel_y,
                    comp->width,
                    comp->height);
                if( idx >= 0 )
                    expand_apply_behavior(state, tree, idx, comp->id);
            }
        }
        break;

    case COMPONENT_TYPE_INV:
        for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
        {
            bg_sid[si] = -1;
            bg_ai[si] = 0;
        }
        if( comp->invSlotGraphic )
        {
            for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
            {
                char const* gname = comp->invSlotGraphic[si];
                if( !gname || gname[0] == '\0' )
                    continue;
                bg_sid[si] = expand_acquire_sprite(state, gname);
                bg_ai[si] = 0;
            }
        }
        {
            int const cols = comp->width > 0 ? comp->width : 4;
            int const rows = comp->height > 0 ? comp->height : 7;
            int const pix_w = cols > 0 ? cols * 32 + (cols - 1) * comp->marginX : 4 * 32;
            int const pix_h = rows > 0 ? rows * 32 + (rows - 1) * comp->marginY : 7 * 32;
            idx = uitree_push_rs_inv(
                tree,
                parent_idx,
                comp->id,
                sidebar_inv_index,
                comp->width,
                comp->height,
                comp->marginX,
                comp->marginY,
                comp->invSlotOffsetX,
                comp->invSlotOffsetY,
                bg_sid,
                bg_ai,
                rel_x,
                rel_y,
                pix_w,
                pix_h);
        }
        if( idx >= 0 )
            expand_apply_behavior(state, tree, idx, comp->id);
        break;

    default:
        break;
    }
}

static void
preload_comp_graphics(
    struct RevConfigUIBuildState* state,
    struct RSCacheDat1A_ConfigComponentList* interfaces,
    struct RSCacheDat1A_ConfigComponent* comp)
{
    if( !state || !interfaces || !comp )
        return;

    if( comp->graphic && comp->graphic[0] != '\0' )
        expand_acquire_sprite(state, comp->graphic);
    if( comp->activeGraphic && comp->activeGraphic[0] != '\0' )
        expand_acquire_sprite(state, comp->activeGraphic);
    if( comp->invSlotGraphic )
    {
        for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
        {
            char const* gname = comp->invSlotGraphic[si];
            if( gname && gname[0] != '\0' )
                expand_acquire_sprite(state, gname);
        }
    }

    if( comp->type != COMPONENT_TYPE_LAYER || !comp->children )
        return;

    for( int i = 0; i < comp->children_count; i++ )
    {
        struct RSCacheDat1A_ConfigComponent* child =
            expand_get_iface_component(interfaces, comp->children[i]);
        if( child )
            preload_comp_graphics(state, interfaces, child);
    }
}

void
revconfig_ui_preload_interface_sprites(struct RevConfigUIBuildState* state)
{
    assert(state);

    struct RSCacheDat1A_ConfigComponentList* interfaces = dat1_buildcache_get_interfaces(state->bc);
    if( !interfaces )
        return;

    for( int i = 0; i < state->component_count; i++ )
    {
        struct RevConfigUIComponentItem const* comp = &state->components[i];
        if( strcmp(comp->type, "sidebar") != 0 || comp->componentno < 0 )
            continue;

        struct RSCacheDat1A_ConfigComponent* root =
            expand_get_iface_component(interfaces, comp->componentno);
        if( root )
            preload_comp_graphics(state, interfaces, root);
    }
}

void
revconfig_ui_expand_sidebar(
    struct RevConfigUIBuildState* state,
    struct UITree* tree,
    int32_t sidebar_idx,
    int componentno,
    int inv_index,
    int sidebar_x,
    int sidebar_y)
{
    if( !state || !tree || !state->bc || componentno < 0 || sidebar_idx < 0 ||
        (uint32_t)sidebar_idx >= tree->component_count )
        return;

    struct RSCacheDat1A_ConfigComponentList* interfaces = dat1_buildcache_get_interfaces(state->bc);
    if( !interfaces )
        return;

    struct RSCacheDat1A_ConfigComponent* root =
        expand_resolve_sidebar_root(state, interfaces, componentno, inv_index);
    if( !root )
        return;

    (void)sidebar_x;
    (void)sidebar_y;
    expand_bake_rs_subtree(state, tree, interfaces, sidebar_idx, root, root->x, root->y, inv_index);
}
