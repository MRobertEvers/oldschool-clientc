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

struct UIBakeContext
{
    struct RevConfigUIBuildState* state;
    struct UITree* tree;
    struct RSCacheDat1A_ConfigComponentList* interfaces;
    int sidebar_inv_index;
};

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

static bool
expand_fill_behavior(
    struct UIBakeContext const* ctx,
    int component_id,
    struct StaticUIBehavior* out)
{
    if( !ctx || !ctx->state || !out || component_id < 0 || !ctx->state->core )
        return false;

    struct ToriAuxLibCore_Component* gc_comp =
        ToriAuxLibCore_ComponentGet(ctx->state->core, component_id);
    if( !gc_comp )
        return false;

    memset(out, 0, sizeof(*out));
    /* Sidebar bake: do not copy interface hide; tab visibility is via sidebar parent. */
    out->hide = 0;
    out->button_type = gc_comp->button_type;
    out->client_code = gc_comp->client_code;
    out->over_color = gc_comp->over_color;
    out->active_color = gc_comp->active_color;
    out->active_over_color = gc_comp->active_over_color;
    out->scripts_count = gc_comp->scripts_count;
    out->scripts = gc_comp->scripts;
    out->scripts_lengths = gc_comp->scripts_lengths;
    out->script_comparator = gc_comp->script_comparator;
    out->script_operand = gc_comp->script_operand;
    return true;
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
    struct UIBakeContext const* ctx,
    int32_t parent_idx,
    struct RSCacheDat1A_ConfigComponent* comp,
    int rel_x,
    int rel_y)
{
    if( !ctx || !ctx->tree || !ctx->interfaces || !comp )
        return;

    struct StaticUIBehavior behavior;
    struct StaticUIBehavior* behavior_ptr = NULL;
    if( expand_fill_behavior(ctx, comp->id, &behavior) )
        behavior_ptr = &behavior;

    switch( comp->type )
    {
    case COMPONENT_TYPE_LAYER:
    {
        struct UINodeSpec spec = { 0 };
        spec.type = UIELEM_RS_LAYER;
        spec.component_id = comp->id;
        spec.x = rel_x;
        spec.y = rel_y;
        spec.width = comp->width;
        spec.height = comp->height;
        spec.behavior = behavior_ptr;

        int32_t layer_idx = uitree_push(ctx->tree, parent_idx, &spec);
        if( layer_idx < 0 || !comp->children )
            return;

        for( int i = 0; i < comp->children_count; i++ )
        {
            struct RSCacheDat1A_ConfigComponent* child =
                expand_get_iface_component(ctx->interfaces, comp->children[i]);
            if( !child )
                continue;

            int cx = (comp->childX ? comp->childX[i] : 0) + child->x;
            int cy = (comp->childY ? comp->childY[i] : 0) + child->y;
            expand_bake_rs_subtree(ctx, layer_idx, child, cx, cy);
        }
        break;
    }

    case COMPONENT_TYPE_GRAPHIC:
    {
        int sid = expand_acquire_sprite(ctx->state, comp->graphic);
        int sid_a = expand_acquire_sprite(ctx->state, comp->activeGraphic);
        if( sid < 0 && sid_a < 0 )
            return;

        struct UINodeSpec spec = { 0 };
        spec.type = UIELEM_RS_GRAPHIC;
        spec.component_id = comp->id;
        spec.x = rel_x;
        spec.y = rel_y;
        spec.width = comp->width;
        spec.height = comp->height;
        spec.u.rs_graphic.scene_id = sid >= 0 ? sid : sid_a;
        spec.u.rs_graphic.atlas_index = 0;
        spec.u.rs_graphic.scene_id_active = sid >= 0 && sid_a >= 0 && sid_a != sid ? sid_a : -1;
        spec.u.rs_graphic.atlas_index_active = 0;
        spec.behavior = behavior_ptr;
        uitree_push(ctx->tree, parent_idx, &spec);
        break;
    }

    case COMPONENT_TYPE_RECT:
        if( comp->fill )
        {
            struct UINodeSpec spec = { 0 };
            spec.type = UIELEM_RS_RECT;
            spec.component_id = comp->id;
            spec.x = rel_x;
            spec.y = rel_y;
            spec.width = comp->width;
            spec.height = comp->height;
            spec.u.rs_rect.color = comp->colour;
            spec.u.rs_rect.filled = comp->fill ? 1 : 0;
            spec.behavior = behavior_ptr;
            uitree_push(ctx->tree, parent_idx, &spec);
        }
        break;

    case COMPONENT_TYPE_TEXT:
    case COMPONENT_TYPE_INV_TEXT:
    {
        int fid = comp->font;
        if( fid < 0 || fid > 3 )
            fid = 1;

        struct UINodeSpec spec = { 0 };
        spec.type = UIELEM_RS_TEXT;
        spec.component_id = comp->id;
        spec.x = rel_x;
        spec.y = rel_y;
        spec.width = comp->width;
        spec.height = comp->height;
        spec.u.rs_text.font_id = fid;
        spec.u.rs_text.color = comp->colour;
        spec.u.rs_text.center = comp->center ? 1 : 0;
        spec.u.rs_text.shadowed = comp->shadowed ? 1 : 0;
        spec.u.rs_text.text = comp->text;
        spec.behavior = behavior_ptr;
        uitree_push(ctx->tree, parent_idx, &spec);
        break;
    }

    case COMPONENT_TYPE_MODEL:
        if( comp->modelType == 1 && ctx->state && ctx->state->core )
        {
            struct ToriAuxLibCore_Model* model =
                ToriAuxLibCore_ModelGet(ctx->state->core, comp->model);
            if( model )
            {
                struct UINodeSpec spec = { 0 };
                spec.type = UIELEM_RS_MODEL;
                spec.component_id = comp->id;
                spec.x = rel_x;
                spec.y = rel_y;
                spec.width = comp->width;
                spec.height = comp->height;
                spec.u.rs_model.gamecache_model_id = comp->model;
                spec.u.rs_model.zoom = comp->zoom;
                spec.u.rs_model.xan = comp->xan;
                spec.u.rs_model.yan = comp->yan;
                spec.behavior = behavior_ptr;
                uitree_push(ctx->tree, parent_idx, &spec);
            }
        }
        break;

    case COMPONENT_TYPE_INV:
    {
        int bg_sid[UI_INV_SLOT_OFFSET_MAX];
        int bg_ai[UI_INV_SLOT_OFFSET_MAX];
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
                bg_sid[si] = expand_acquire_sprite(ctx->state, gname);
                bg_ai[si] = 0;
            }
        }

        int const cols = comp->width > 0 ? comp->width : 4;
        int const rows = comp->height > 0 ? comp->height : 7;
        int const pix_w = cols > 0 ? cols * 32 + (cols - 1) * comp->marginX : 4 * 32;
        int const pix_h = rows > 0 ? rows * 32 + (rows - 1) * comp->marginY : 7 * 32;

        struct UINodeSpec spec = { 0 };
        spec.type = UIELEM_RS_INV;
        spec.component_id = comp->id;
        spec.x = rel_x;
        spec.y = rel_y;
        spec.width = pix_w;
        spec.height = pix_h;
        spec.u.rs_inv.inv_index = ctx->sidebar_inv_index;
        spec.u.rs_inv.cols = comp->width;
        spec.u.rs_inv.rows = comp->height;
        spec.u.rs_inv.margin_x = comp->marginX;
        spec.u.rs_inv.margin_y = comp->marginY;
        spec.u.rs_inv.inv_slot_offset_x = comp->invSlotOffsetX;
        spec.u.rs_inv.inv_slot_offset_y = comp->invSlotOffsetY;
        spec.u.rs_inv.inv_slot_bg_scene_id = bg_sid;
        spec.u.rs_inv.inv_slot_bg_atlas_index = bg_ai;
        spec.behavior = behavior_ptr;
        uitree_push(ctx->tree, parent_idx, &spec);
        break;
    }

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

    struct UIBakeContext ctx = {
        .state = state,
        .tree = tree,
        .interfaces = interfaces,
        .sidebar_inv_index = inv_index,
    };
    expand_bake_rs_subtree(&ctx, sidebar_idx, root, root->x, root->y);
}
