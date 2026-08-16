#include "runescape_cs2_host.h"

#include "runescape.h"

#include "buildcache/dat2_buildcache.h"
#include "games/ie_enum_lookup.h"
#include "games/ie_struct_lookup.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toriauxlib/vm/toriauxlibvm.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_scene.h"
#include "ui/rs_inv_container.h"
#include "ui/ui_scroll.h"
#include "ui/uitree.h"
#include "vm/cs2vmx.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Helpers
 * ========================================================================= */

static struct UITree*
rs_cs2_tree(struct GameRunescapeCS2Host* host)
{
    return host && host->game ? host->game->ui_tree : NULL;
}

static struct ToriAuxLibCore*
rs_cs2_core(struct GameRunescapeCS2Host* host)
{
    return host && host->game ? host->game->core : NULL;
}

static struct ToriDraw_Scene*
rs_cs2_scene(struct GameRunescapeCS2Host* host)
{
    return host && host->game ? host->game->scene : NULL;
}

static struct ToriAuxLibVM*
rs_cs2_vm(struct GameRunescapeCS2Host* host)
{
    return host && host->game ? host->game->vm : NULL;
}

static struct ToriAuxLibCache*
rs_cs2_cache(struct GameRunescapeCS2Host* host)
{
    assert(host);
    if( !host->game || !host->game->td )
        return NULL;
    return ToriAuxLibTD_C(host->game->td);
}

static struct Dat2BuildCache*
rs_cs2_dat2(struct GameRunescapeCS2Host* host)
{
    struct ToriAuxLibCache* cache = rs_cs2_cache(host);
    return cache ? dat2(cache) : NULL;
}

static struct UITreeScrollState
rs_cs2_scroll_view(struct GameRunescapeCS2Host* host)
{
    struct UITreeScrollState view = { 0 };
    if( host && host->game )
        view = ui_scroll_runtime_view(&host->game->ui_scroll);
    return view;
}

static int32_t
rs_cs2_find_node(
    struct GameRunescapeCS2Host* host,
    int component_id)
{
    struct UITree* tree = rs_cs2_tree(host);
    assert(tree);
    if( component_id < 0 )
        return -1;
    return uitree_find_by_component_id(tree, component_id);
}

static struct StaticUIComponent*
rs_cs2_node(
    struct GameRunescapeCS2Host* host,
    int component_id)
{
    struct UITree* tree = rs_cs2_tree(host);
    int32_t idx = rs_cs2_find_node(host, component_id);
    assert(tree);
    if( idx < 0 )
        return NULL;
    return &tree->components[idx];
}

static int
rs_cs2_yield(
    struct GameRunescapeCS2Host* host,
    struct CS2VM_HostRequest const* request)
{
    assert(host);
    assert(request);
    host->pending = *request;
    host->has_pending = true;
    return CS2VM_EXECNO_YIELD;
}

/** Yield when an interface group should exist but its component is not in the UITree yet. */
static int
rs_cs2_yield_if_group_missing(
    struct GameRunescapeCS2Host* host,
    int component_id,
    struct CS2VM_HostRequest const* request)
{
    int group_id;
    struct ToriAuxLibCore* core;

    assert(host);
    assert(request);

    group_id = (component_id >> 16) & 0xffff;
    if( component_id <= 0 || group_id <= 0 )
        return CS2VM_EXECNO_OK;

    if( rs_cs2_find_node(host, component_id) >= 0 )
        return CS2VM_EXECNO_OK;

    /* Component may already be in core but not baked into the tree yet — still yield. */
    core = rs_cs2_core(host);
    if( core && ToriAuxLibCore_ComponentHas(core, component_id) )
        return rs_cs2_yield(host, request);

    return rs_cs2_yield(host, request);
}

static bool
rs_cs2_sprite_ready(
    struct GameRunescapeCS2Host* host,
    int graphic_id)
{
    struct ToriDraw_Scene* scene;
    struct ToriAuxLibCore* core;

    if( graphic_id < 0 )
        return true;

    scene = rs_cs2_scene(host);
    if( scene && ToriDraw_SceneSpriteHas(scene, graphic_id) )
        return true;

    core = rs_cs2_core(host);
    if( core && ToriAuxLibCore_SpriteHas(core, graphic_id) )
    {
        /* In core but not scene — task layer must promote; treat as not ready. */
        return false;
    }
    return false;
}

static bool
rs_cs2_font_ready(
    struct GameRunescapeCS2Host* host,
    int font_id)
{
    struct ToriDraw_Scene* scene = rs_cs2_scene(host);
    if( font_id < 0 )
        return true;
    if( scene && ToriDraw_SceneFontGet(scene, font_id) )
        return true;
    return false;
}

static bool
rs_cs2_model_ready(
    struct GameRunescapeCS2Host* host,
    int model_id)
{
    struct ToriAuxLibCore* core = rs_cs2_core(host);
    if( model_id < 0 )
        return true;
    return core && ToriAuxLibCore_ModelHas(core, model_id);
}

static bool
rs_cs2_resolve_obj_icon(
    struct GameRunescapeCS2Host* host,
    int obj_id,
    int* out_scene_id,
    int* out_atlas_index)
{
    struct GameRunescape* game;

    if( out_scene_id )
        *out_scene_id = -1;
    if( out_atlas_index )
        *out_atlas_index = 0;
    assert(host);
    if( !host->game || obj_id <= 0 )
        return false;

    game = host->game;
    for( int src = 0; src < game->inv_data.source_count; src++ )
    {
        struct RSInvContainer const* container;
        if( !game->inv_data.sources[src].used )
            continue;
        container = rs_inv_container_find(&game->inv_data.store, game->inv_data.sources[src].container_id);
        assert(container);

        for( int slot = 0; slot < container->slot_count; slot++ )
        {
            if( container->obj_id[slot] != obj_id )
                continue;
            if( container->scene_id[slot] < 0 )
                continue;
            if( out_scene_id )
                *out_scene_id = container->scene_id[slot];
            if( out_atlas_index )
                *out_atlas_index = container->atlas_index[slot];
            return true;
        }
    }
    return false;
}

static void
rs_cs2_apply_op(
    struct UITree* tree,
    int component_id,
    int index,
    char const* text)
{
    int32_t idx;
    assert(tree);
    if( index < 1 || index > UITREE_MENU_OPTION_SLOTS )
        return;
    idx = uitree_find_by_component_id(tree, component_id);
    if( idx < 0 )
        return;
    strncpy(
        tree->components[idx].menu_options.ops[index - 1],
        text ? text : "",
        UITREE_MENU_OPTION_LEN - 1);
    tree->components[idx].menu_options.ops[index - 1][UITREE_MENU_OPTION_LEN - 1] = '\0';
    uitree_mark_node_dirty(tree, idx);
}

static void
rs_cs2_set_cc_target(
    struct CS2VMX* vm,
    int dot_operand,
    int component_id)
{
    CS2VMX_SetTargetComponentId(vm, dot_operand, component_id);
}

static int
rs_cs2_collect_dynamic_children(
    struct UITree* tree,
    int parent_id,
    int start_index,
    int* out_indices,
    int out_cap)
{
    int32_t parent_idx;
    int count = 0;

    assert(tree);
    assert(out_indices);
    if( out_cap <= 0 )
        return 0;

    parent_idx = uitree_find_by_component_id(tree, parent_id);
    if( parent_idx < 0 )
        return 0;

    for( int32_t child = tree->components[parent_idx].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        struct StaticUIComponent const* c = &tree->components[child];
        assert(c);
        if( c->dynamic_child_index < start_index )
            continue;
        if( count >= out_cap )
            break;
        out_indices[count++] = (int)child;
    }
    return count;
}

static int
rs_cs2_parent_component_id(
    struct UITree* tree,
    int component_id)
{
    int32_t idx;
    int32_t parent;
    assert(tree);

    idx = uitree_find_by_component_id(tree, component_id);
    if( idx < 0 )
        return -1;
    parent = tree->components[idx].parent;
    if( parent < 0 || (uint32_t)parent >= tree->component_count )
        return -1;
    return tree->components[parent].component_id;
}

/* =========================================================================
 * Init / Tick
 * ========================================================================= */

void
GameRunescape_CS2HostInit(
    struct GameRunescapeCS2Host* host,
    struct GameRunescape* game)
{
    assert(host);

    memset(host, 0, sizeof(*host));
    host->game = game;
    host->client_clock = 100;
    host->client_type = 80;
    if( game )
    {
        if( game->view_port )
        {
            host->viewport_w = game->view_port->width;
            host->viewport_h = game->view_port->height;
        }
    }
}

void
GameRunescape_CS2HostTick(struct GameRunescapeCS2Host* host)
{
    assert(host);

    host->client_clock++;
}

/* =========================================================================
 * Inventory
 * ========================================================================= */

static int
rs_cs2_inv_size(struct GameRunescapeCS2Host* host, int inv_id)
{
    struct RSInvContainer const* container;
    assert(host);
    if( !host->game || inv_id < 0 )
        return 0;
    container = rs_inv_container_find(&host->game->inv_data.store, inv_id);
    return container ? container->slot_count : 0;
}

static int
rs_cs2_inv_get_obj(struct GameRunescapeCS2Host* host, int inv_id, int slot)
{
    struct RSInvContainer const* container;
    assert(host);
    if( !host->game || inv_id < 0 || slot < 0 )
        return -1;
    container = rs_inv_container_find(&host->game->inv_data.store, inv_id);
    assert(container);
    if( slot >= container->slot_count )
        return -1;
    return container->obj_id[slot];
}

static int
rs_cs2_inv_get_num(struct GameRunescapeCS2Host* host, int inv_id, int slot)
{
    struct RSInvContainer const* container;
    assert(host);
    if( !host->game || inv_id < 0 || slot < 0 )
        return 0;
    container = rs_inv_container_find(&host->game->inv_data.store, inv_id);
    assert(container);
    if( slot >= container->slot_count )
        return 0;
    if( container->obj_id[slot] <= 0 )
        return 0;
    return container->obj_count[slot] > 0 ? container->obj_count[slot] : 1;
}

static int
rs_cs2_inv_total(struct GameRunescapeCS2Host* host, int inv_id, int item_id)
{
    struct RSInvContainer const* container;
    int total = 0;
    assert(host);
    if( !host->game || inv_id < 0 || item_id <= 0 )
        return 0;
    container = rs_inv_container_find(&host->game->inv_data.store, inv_id);
    assert(container);

    for( int slot = 0; slot < container->slot_count; slot++ )
    {
        if( container->obj_id[slot] == item_id )
            total += container->obj_count[slot] > 0 ? container->obj_count[slot] : 1;
    }
    return total;
}

/* =========================================================================
 * Exec handlers
 * ========================================================================= */

static int
exec_push_script(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    int script_id)
{
    struct ToriAuxLibCore* core = rs_cs2_core(host);
    struct ToriAuxLibCore_ClientScript* cs = NULL;

    if( core )
        cs = ToriAuxLibCore_ClientScriptGet(core, script_id);
    if( !cs )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_PUSHSCRIPT;
        req.u.push_script.script_id = script_id;
        return rs_cs2_yield(host, &req);
    }
    return CS2VMX_PushCallScript(vm, &cs->script);
}

static int
exec_para_height(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_ParaHeight request,
    int is_width)
{
    int result = 0;
    char const* text = request.text ? request.text : "";
    if( text[0] != '\0' )
    {
        struct ToriDraw_Font* font =
            rs_cs2_scene(host) ? ToriDraw_SceneFontGet(rs_cs2_scene(host), request.font_id) : NULL;
        if( !font )
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = is_width ? CS2VM_HOST_REQUEST_PARAWIDTH : CS2VM_HOST_REQUEST_PARAHEIGHT;
            req.u.para_height = request;
            return rs_cs2_yield(host, &req);
        }
        result = is_width ? ToriDraw2D_WrapMaxLineWidth(font, text, request.max_width)
                          : ToriDraw2D_WrapLineCount(font, text, request.max_width);
    }
    return CS2VMX_PushInt(vm, result);
}

static int
exec_vars_read_varp(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    int varp_id)
{
    int value = 0;
    struct ToriAuxLibVM* aux = rs_cs2_vm(host);
    if( aux )
        value = ToriAuxLibVM_GetVarp(aux, varp_id);
    return CS2VMX_PushInt(vm, value);
}

static int
exec_vars_read_varbit(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    int varbit_id)
{
    int value = 0;
    struct ToriAuxLibVM* aux = rs_cs2_vm(host);
    if( aux )
        value = ToriAuxLibVM_GetVarbit(aux, varbit_id);
    return CS2VMX_PushInt(vm, value);
}

static int
exec_enum_lookup(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_EnumLookup request)
{
    struct Dat2BuildCache* bc = rs_cs2_dat2(host);
    if( !bc || !dat2_buildcache_enum_get(bc, request.enum_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_ENUM_LOOKUP;
        req.u.enum_lookup = request;
        return rs_cs2_yield(host, &req);
    }

    if( request.output_type == (int)'s' )
    {
        char const* value = ie_enum_lookup_string(
            bc, request.input_type, request.output_type, request.enum_id, request.key);
        return CS2VMX_PushStr(vm, strdup(value ? value : "null"));
    }

    return CS2VMX_PushInt(
        vm,
        ie_enum_lookup(bc, request.input_type, request.output_type, request.enum_id, request.key));
}

static int
exec_enum_output_count(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_EnumGetOutputCount request)
{
    struct Dat2BuildCache* bc = rs_cs2_dat2(host);
    if( !bc || !dat2_buildcache_enum_get(bc, request.enum_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT;
        req.u.enum_get_output_count = request;
        return rs_cs2_yield(host, &req);
    }
    return CS2VMX_PushInt(vm, ie_enum_output_count(bc, request.enum_id));
}

static int
exec_struct_param(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_StructParam request)
{
    bool is_string = false;
    int intval = 0;
    char const* strval = NULL;
    bool found;
    struct Dat2BuildCache* bc = rs_cs2_dat2(host);

    if( !bc || !dat2_buildcache_struct_get(bc, request.struct_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_STRUCT_PARAM;
        req.u.struct_param = request;
        return rs_cs2_yield(host, &req);
    }

    found = ie_struct_param_lookup(
        bc, request.struct_id, request.param_id, &is_string, &intval, &strval);
    if( found && is_string )
        return CS2VMX_PushStr(vm, strdup(strval ? strval : ""));
    if( found )
        return CS2VMX_PushInt(vm, intval);
    return CS2VMX_PushInt(vm, 0);
}

static int
exec_oc_int_param(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_OC_IntParam request)
{
    struct Dat2BuildCache* bc = rs_cs2_dat2(host);
    struct RSCacheDat2A_ConfigObject* obj = bc ? dat2_buildcache_object_get(bc, request.item_id) : NULL;
    int value = 0;

    if( !obj )
    {
        struct ToriAuxLibCore_Objtype* core_obj =
            rs_cs2_core(host) ? ToriAuxLibCore_ObjtypeGet(rs_cs2_core(host), request.item_id) : NULL;
        if( !core_obj )
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = CS2VM_HOST_REQUEST_OC_INT_PARAM;
            req.u.oc_int_param = request;
            return rs_cs2_yield(host, &req);
        }
        switch( request.field )
        {
        case CS2VM_OC_INT_STACKABLE:
            value = core_obj->stackable;
            break;
        case CS2VM_OC_INT_ID:
            value = core_obj->id;
            break;
        case CS2VM_OC_INT_COST:
        case CS2VM_OC_INT_MEMBERS:
        default:
            value = 0;
            break;
        }
        return CS2VMX_PushInt(vm, value);
    }

    switch( request.field )
    {
    case CS2VM_OC_INT_COST:
        value = obj->cost;
        break;
    case CS2VM_OC_INT_STACKABLE:
        value = obj->stacking_behaviour;
        break;
    case CS2VM_OC_INT_MEMBERS:
        value = obj->is_members ? 1 : 0;
        break;
    case CS2VM_OC_INT_ID:
        value = obj->_id;
        break;
    }
    return CS2VMX_PushInt(vm, value);
}

static int
exec_oc_name(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_OC_Name request)
{
    struct ToriAuxLibCore* core = rs_cs2_core(host);
    struct ToriAuxLibCore_Objtype* obj = core ? ToriAuxLibCore_ObjtypeGet(core, request.item_id) : NULL;
    char const* name = "null";

    if( !obj )
    {
        struct Dat2BuildCache* bc = rs_cs2_dat2(host);
        struct RSCacheDat2A_ConfigObject* cfg =
            bc ? dat2_buildcache_object_get(bc, request.item_id) : NULL;
        if( !cfg )
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = CS2VM_HOST_REQUEST_OC_NAME;
            req.u.oc_name = request;
            return rs_cs2_yield(host, &req);
        }
        if( cfg->name && cfg->name[0] )
            name = cfg->name;
        return CS2VMX_PushStr(vm, strdup(name));
    }

    if( obj->name[0] != '\0' )
        name = obj->name;
    return CS2VMX_PushStr(vm, strdup(name));
}

static int
exec_oc_unplaceholder(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_OC_Unplaceholder request)
{
    /* Placeholder remapping needs full object config; yield if missing, else identity. */
    struct ToriAuxLibCore* core = rs_cs2_core(host);
    struct Dat2BuildCache* bc = rs_cs2_dat2(host);
    if( (core && ToriAuxLibCore_ObjtypeHas(core, request.item_id)) ||
        (bc && dat2_buildcache_object_get(bc, request.item_id)) )
        return CS2VMX_PushInt(vm, request.item_id);

    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER;
        req.u.oc_unplaceholder = request;
        return rs_cs2_yield(host, &req);
    }
}

static int
exec_set_graphic(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetGraphic request)
{
    struct UITree* tree = rs_cs2_tree(host);
    (void)vm;

    assert(tree);

    if( request.graphic_id >= 0 && !rs_cs2_sprite_ready(host, request.graphic_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_CC_SETGRAPHIC;
        req.u.cc_set_graphic = request;
        return rs_cs2_yield(host, &req);
    }

    (void)uitree_apply_graphic(tree, request.component_id, request.graphic_id);
    return CS2VM_EXECNO_OK;
}

static int
exec_set_object(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    int component_id,
    int obj_id,
    int count)
{
    struct UITree* tree = rs_cs2_tree(host);
    int scene_id = -1;
    int atlas_index = 0;
    (void)vm;

    assert(tree);

    if( obj_id <= 0 )
    {
        (void)uitree_apply_object(tree, component_id, 0, 0, -1, 0);
        return CS2VM_EXECNO_OK;
    }

    if( !rs_cs2_resolve_obj_icon(host, obj_id, &scene_id, &atlas_index) )
    {
        /* Also accept if objtype is known but icon not yet rasterized — still yield. */
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_CC_SETOBJECT;
        req.u.cc_set_object.component_id = component_id;
        req.u.cc_set_object.obj_id = obj_id;
        req.u.cc_set_object.count = count;
        return rs_cs2_yield(host, &req);
    }

    (void)uitree_apply_object(tree, component_id, obj_id, count, scene_id, atlas_index);
    return CS2VM_EXECNO_OK;
}

static int
exec_set_text_font(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetTextFont request)
{
    struct StaticUIComponent* node;
    (void)vm;

    if( request.font_id >= 0 && !rs_cs2_font_ready(host, request.font_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_CC_SETTEXTFONT;
        req.u.cc_set_text_font = request;
        return rs_cs2_yield(host, &req);
    }

    node = rs_cs2_node(host, request.component_id);
    if( node && node->type == UIELEM_RS_TEXT )
    {
        node->u.rs_text.font_id = request.font_id;
        uitree_mark_node_dirty(rs_cs2_tree(host), rs_cs2_find_node(host, request.component_id));
    }
    return CS2VM_EXECNO_OK;
}

static int
exec_cc_create(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_Create request)
{
    struct UITree* tree = rs_cs2_tree(host);
    int parent_id = request.parent_id;
    int32_t parent_idx;
    int32_t child_idx;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };

    yield_req.kind = CS2VM_HOST_REQUEST_CC_CREATE;
    yield_req.u.cc_create = request;
    yield_res = rs_cs2_yield_if_group_missing(host, parent_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
        return yield_res;

    assert(tree);

    parent_idx = uitree_find_by_component_id(tree, parent_id);
    if( parent_idx < 0 )
    {
        if( parent_id > 0 )
        {
            /* Group claimed ready earlier but parent still missing — yield again. */
            return rs_cs2_yield(host, &yield_req);
        }
        return CS2VM_EXECNO_OK;
    }

    child_idx = uitree_cc_create(
        tree, parent_idx, parent_id, request.component_type, request.child_index);
    if( child_idx < 0 )
        return CS2VM_EXECNO_ERROR;

    rs_cs2_set_cc_target(vm, request.dot_operand, tree->components[child_idx].component_id);
    return CS2VM_EXECNO_OK;
}

static int
exec_cc_find(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_Find request)
{
    struct UITree* tree = rs_cs2_tree(host);
    int found = 0;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };
    int32_t parent_idx;

    yield_req.kind = CS2VM_HOST_REQUEST_CC_FIND;
    yield_req.u.cc_find = request;
    yield_res = rs_cs2_yield_if_group_missing(host, request.parent_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
        return yield_res;

    if( tree )
    {
        parent_idx = uitree_find_by_component_id(tree, request.parent_id);
        if( parent_idx >= 0 )
        {
            int32_t child_idx = uitree_find_child_by_subid(
                tree, parent_idx, request.parent_id, request.sub_id);
            if( child_idx >= 0 )
            {
                rs_cs2_set_cc_target(vm, request.dot_operand, tree->components[child_idx].component_id);
                found = 1;
            }
        }
        else if( request.parent_id > 0 )
            return rs_cs2_yield(host, &yield_req);
    }

    return CS2VMX_PushInt(vm, found);
}

static int
exec_if_find(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_TargetFind request)
{
    int found = 0;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };

    yield_req.kind = CS2VM_HOST_REQUEST_IF_FIND;
    yield_req.u.if_find = request;
    yield_res = rs_cs2_yield_if_group_missing(host, request.component_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
        return yield_res;

    if( rs_cs2_find_node(host, request.component_id) >= 0 )
    {
        rs_cs2_set_cc_target(vm, request.dot_operand, request.component_id);
        found = 1;
    }
    else if( request.component_id > 0 )
        return rs_cs2_yield(host, &yield_req);

    return CS2VMX_PushInt(vm, found);
}

static int
exec_children_find(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    int parent_id,
    int start_index,
    int set_target_dot,
    int dot_operand,
    enum CS2VM_HostRequestKind kind)
{
    struct UITree* tree = rs_cs2_tree(host);
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };

    yield_req.kind = kind;
    if( kind == CS2VM_HOST_REQUEST_CC_CHILDREN_FIND )
    {
        yield_req.u.cc_children_find.parent_id = parent_id;
        yield_req.u.cc_children_find.start_index = start_index;
    }
    else
    {
        yield_req.u.if_children_find.uid = parent_id;
        yield_req.u.if_children_find.start_index = start_index;
        yield_req.u.if_children_find.dot_operand = dot_operand;
    }

    yield_res = rs_cs2_yield_if_group_missing(host, parent_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
        return yield_res;

    CS2VMX_ResetChildrenIter(vm);
    if( tree )
    {
        if( uitree_find_by_component_id(tree, parent_id) < 0 && parent_id > 0 )
            return rs_cs2_yield(host, &yield_req);

        vm->children_iter_count = rs_cs2_collect_dynamic_children(
            tree,
            parent_id,
            start_index,
            vm->children_iter_indices,
            CS2VMX_CHILDREN_ITER_MAX);

        if( set_target_dot && uitree_find_by_component_id(tree, parent_id) >= 0 )
            rs_cs2_set_cc_target(vm, dot_operand, parent_id);
    }
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_model(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_WidgetSetModel request)
{
    (void)vm;
    if( request.model_id >= 0 && !rs_cs2_model_ready(host, request.model_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL;
        req.u.widget_set_model = request;
        return rs_cs2_yield(host, &req);
    }
    if( rs_cs2_tree(host) )
        (void)uitree_apply_model(rs_cs2_tree(host), request.component_id, request.model_id);
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_model_kind(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_WidgetSetModelKind request)
{
    (void)vm;
    if( request.model_kind == CS2VM_MODEL_KIND_PLAIN && request.model_id >= 0 &&
        !rs_cs2_model_ready(host, request.model_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND;
        req.u.widget_set_model_kind = request;
        return rs_cs2_yield(host, &req);
    }
    if( request.model_kind == CS2VM_MODEL_KIND_PLAIN && rs_cs2_tree(host) )
        (void)uitree_apply_model(rs_cs2_tree(host), request.component_id, request.model_id);
    /* NPC/player heads: apply model_id when present; full appearance load is task-layer. */
    else if( rs_cs2_tree(host) && request.model_id >= 0 )
        (void)uitree_apply_model(rs_cs2_tree(host), request.component_id, request.model_id);
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_int(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_WidgetSetInt request)
{
    struct StaticUIComponent* node = rs_cs2_node(host, request.component_id);
    int32_t idx;
    (void)vm;
    assert(node);

    idx = rs_cs2_find_node(host, request.component_id);

    switch( request.field )
    {
    case CS2VM_WIDGET_INT_HFLIP:
        if( node->type == UIELEM_RS_GRAPHIC )
            node->u.rs_graphic.flip_h = request.value ? 1 : 0;
        break;
    case CS2VM_WIDGET_INT_VFLIP:
        if( node->type == UIELEM_RS_GRAPHIC )
            node->u.rs_graphic.flip_v = request.value ? 1 : 0;
        break;
    case CS2VM_WIDGET_INT_FILL_COLOUR:
        (void)uitree_apply_colour(rs_cs2_tree(host), request.component_id, request.value);
        break;
    case CS2VM_WIDGET_INT_LINE_WIDTH:
        if( node->type == UIELEM_RS_LINE )
            node->u.rs_line.line_width = request.value;
        break;
    case CS2VM_WIDGET_INT_LINE_DIRECTION:
        if( node->type == UIELEM_RS_LINE )
            node->u.rs_line.horizontal = request.value ? 1 : 0;
        break;
    case CS2VM_WIDGET_INT_NO_CLICK_THROUGH:
        node->no_click_through = request.value ? 1 : 0;
        break;
    case CS2VM_WIDGET_INT_CLICKMASK:
        (void)uitree_apply_click_mask(rs_cs2_tree(host), request.component_id, request.value);
        break;
    case CS2VM_WIDGET_INT_DRAG_DEAD_ZONE:
        node->drag_dead_zone = (uint8_t)request.value;
        break;
    case CS2VM_WIDGET_INT_DRAG_DEAD_TIME:
        node->drag_dead_time = (uint8_t)request.value;
        break;
    case CS2VM_WIDGET_INT_MODEL_TRANSPARENT:
        (void)uitree_apply_model_transparent(
            rs_cs2_tree(host), request.component_id, request.value);
        break;
    case CS2VM_WIDGET_INT_MODEL_ORTHOG:
    case CS2VM_WIDGET_INT_MODEL_ANIM:
    case CS2VM_WIDGET_INT_ANGLE_2D:
    case CS2VM_WIDGET_INT_FILL_MODE:
    case CS2VM_WIDGET_INT_TRANS_BOT:
    case CS2VM_WIDGET_INT_NO_SCROLL_THROUGH:
    case CS2VM_WIDGET_INT_PINCH:
    case CS2VM_WIDGET_INT_RESUME_PAUSEBUTTON:
        /* UITree lacks these fields; accept no-op. */
        break;
    default:
        break;
    }
    if( idx >= 0 )
        uitree_mark_node_dirty(rs_cs2_tree(host), idx);
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_model_angle(
    struct GameRunescapeCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_WidgetSetModelAngle request)
{
    struct StaticUIComponent* node = rs_cs2_node(host, request.component_id);
    (void)vm;
    assert(node);
    if( node->type != UIELEM_RS_MODEL )
        return CS2VM_EXECNO_OK;
    node->u.rs_model.xan = request.angle_x;
    node->u.rs_model.yan = request.angle_y;
    if( request.zoom > 0 )
        node->u.rs_model.zoom = request.zoom;
    uitree_mark_node_dirty(rs_cs2_tree(host), rs_cs2_find_node(host, request.component_id));
    return CS2VM_EXECNO_OK;
}

static int
exec_set_on_inv_transmit(
    struct GameRunescapeCS2Host* host,
    struct CS2VM_HostRequest_IF_SetOnInvTransmit const* request)
{
    struct GameRunescapeCS2InvTransmitHook* hook;
    assert(host);
    if( host->inv_transmit_hook_count >= RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX )
        return CS2VM_EXECNO_OK;
    hook = &host->inv_transmit_hooks[host->inv_transmit_hook_count++];
    memset(hook, 0, sizeof(*hook));
    hook->component_id = request->component_id;
    hook->script_id = request->script_id;
    hook->int_arg_count = request->int_arg_count;
    if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
        hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
    memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
    hook->trigger_count = request->trigger_count;
    if( hook->trigger_count > RS_CS2_HOST_TRANSMIT_TRIGGER_MAX )
        hook->trigger_count = RS_CS2_HOST_TRANSMIT_TRIGGER_MAX;
    if( request->trigger_ids && hook->trigger_count > 0 )
        memcpy(hook->trigger_ids, request->trigger_ids, (size_t)hook->trigger_count * sizeof(int));
    return CS2VM_EXECNO_OK;
}

static int
exec_set_on_var_transmit(
    struct GameRunescapeCS2Host* host,
    struct CS2VM_HostRequest_IF_SetOnVarTransmit const* request)
{
    struct GameRunescapeCS2VarTransmitHook* hook;
    assert(host);
    if( host->var_transmit_hook_count >= RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX )
        return CS2VM_EXECNO_OK;
    hook = &host->var_transmit_hooks[host->var_transmit_hook_count++];
    memset(hook, 0, sizeof(*hook));
    hook->component_id = request->component_id;
    hook->script_id = request->script_id;
    hook->int_arg_count = request->int_arg_count;
    if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
        hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
    memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
    hook->trigger_count = request->trigger_count;
    if( hook->trigger_count > RS_CS2_HOST_TRANSMIT_TRIGGER_MAX )
        hook->trigger_count = RS_CS2_HOST_TRANSMIT_TRIGGER_MAX;
    if( request->trigger_ids && hook->trigger_count > 0 )
        memcpy(hook->trigger_ids, request->trigger_ids, (size_t)hook->trigger_count * sizeof(int));
    return CS2VM_EXECNO_OK;
}

/* =========================================================================
 * Main dispatcher
 * ========================================================================= */

int
GameRunescape_CS2HostExec(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request)
{
    struct GameRunescapeCS2Host* host;
    struct UITree* tree;
    struct StaticUIComponent* node;
    struct UITreeScrollState scroll;

    assert(vm);
    if( !request )
        return CS2VM_EXECNO_ERROR;

    host = (struct GameRunescapeCS2Host*)CS2VM_USER(vm);
    if( !host )
    {
        fprintf(stderr, "GameRunescape_CS2HostExec: CS2VM_USER(vm) is NULL\n");
        return CS2VM_EXECNO_ERROR;
    }

    tree = rs_cs2_tree(host);
    scroll = rs_cs2_scroll_view(host);

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_PUSHSCRIPT:
        return exec_push_script(host, vm, request->u.push_script.script_id);

    case CS2VM_HOST_REQUEST_INVS_GET_SIZE:
        return CS2VMX_PushInt(vm, rs_cs2_inv_size(host, request->u.invs_get_size.inv_id));

    case CS2VM_HOST_REQUEST_INVS_GET_OBJ:
        return CS2VMX_PushInt(
            vm,
            rs_cs2_inv_get_obj(
                host, request->u.invs_get_obj.inv_id, request->u.invs_get_obj.slot));

    case CS2VM_HOST_REQUEST_INVS_GET_NUM:
        return CS2VMX_PushInt(
            vm,
            rs_cs2_inv_get_num(
                host, request->u.invs_get_num.inv_id, request->u.invs_get_num.slot));

    case CS2VM_HOST_REQUEST_INVS_GET_TOTAL:
        return CS2VMX_PushInt(
            vm,
            rs_cs2_inv_total(
                host, request->u.invs_get_total.inv_id, request->u.invs_get_total.item_id));

    case CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR:
        return exec_vars_read_varp(host, vm, request->u.vars_read_varp.varp_id);

    case CS2VM_HOST_REQUEST_VARS_READ_VARBIT:
        return exec_vars_read_varbit(host, vm, request->u.vars_read_varbit.varbit_id);

    case CS2VM_HOST_REQUEST_VARS_READ_VARC_INT:
    {
        int id = request->u.vars_read_varc_int.varc_id;
        int value = 0;
        if( id >= 0 && id < RS_CS2_HOST_VARC_INT_MAX )
            value = host->varc_int[id];
        return CS2VMX_PushInt(vm, value);
    }

    case CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING:
    {
        int id = request->u.vars_read_varc_string.varc_id;
        char* value = (char*)"";
        if( id >= 0 && id < RS_CS2_HOST_VARC_STRING_MAX )
            value = host->varc_string[id];
        return CS2VMX_PushStr(vm, value);
    }

    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_INT:
    {
        int id = request->u.vars_write_varc_int.varc_id;
        if( id >= 0 && id < RS_CS2_HOST_VARC_INT_MAX )
            host->varc_int[id] = request->u.vars_write_varc_int.value;
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_STRING:
    {
        int id = request->u.vars_write_varc_string.varc_id;
        if( id >= 0 && id < RS_CS2_HOST_VARC_STRING_MAX )
        {
            strncpy(
                host->varc_string[id],
                request->u.vars_write_varc_string.value
                    ? request->u.vars_write_varc_string.value
                    : "",
                RS_CS2_HOST_VARC_STRING_LEN - 1);
            host->varc_string[id][RS_CS2_HOST_VARC_STRING_LEN - 1] = '\0';
        }
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_ENUM_LOOKUP:
        return exec_enum_lookup(host, vm, request->u.enum_lookup);

    case CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT:
        return exec_enum_output_count(host, vm, request->u.enum_get_output_count);

    case CS2VM_HOST_REQUEST_STRUCT_PARAM:
        return exec_struct_param(host, vm, request->u.struct_param);

    case CS2VM_HOST_REQUEST_OC_INT_PARAM:
        return exec_oc_int_param(host, vm, request->u.oc_int_param);

    case CS2VM_HOST_REQUEST_OC_NAME:
        return exec_oc_name(host, vm, request->u.oc_name);

    case CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER:
        return exec_oc_unplaceholder(host, vm, request->u.oc_unplaceholder);

    case CS2VM_HOST_REQUEST_OC_PARAM:
        fprintf(
            stderr,
            "GameRunescape_CS2HostExec: OC_PARAM not implemented (item=%d param=%d)\n",
            request->u.oc_param.item_id,
            request->u.oc_param.param_id);
        return CS2VM_EXECNO_ERROR;

    case CS2VM_HOST_REQUEST_PARAHEIGHT:
        return exec_para_height(host, vm, request->u.para_height, 0);

    case CS2VM_HOST_REQUEST_PARAWIDTH:
        return exec_para_height(host, vm, request->u.para_height, 1);

    case CS2VM_HOST_REQUEST_CLIENTCLOCK:
        return CS2VMX_PushInt(vm, host->client_clock);

    case CS2VM_HOST_REQUEST_IF_SETON_DISCARD:
    case CS2VM_HOST_REQUEST_CC_SETON_DISCARD:
        return CS2VM_EXECNO_OK;

    /* ---- IF getters ---- */
    case CS2VM_HOST_REQUEST_IF_GETWIDTH:
        return CS2VMX_PushInt(
            vm, tree ? uitree_get_layout_width(tree, request->u.if_get_width.component_id) : 0);

    case CS2VM_HOST_REQUEST_IF_GETHEIGHT:
        return CS2VMX_PushInt(
            vm, tree ? uitree_get_layout_height(tree, request->u.if_get_height.component_id) : 0);

    case CS2VM_HOST_REQUEST_IF_GETX:
        node = rs_cs2_node(host, request->u.if_getx.component_id);
        return CS2VMX_PushInt(vm, node ? node->position.abs_x : 0);

    case CS2VM_HOST_REQUEST_IF_GETY:
        node = rs_cs2_node(host, request->u.if_get_width.component_id);
        return CS2VMX_PushInt(vm, node ? node->position.abs_y : 0);

    case CS2VM_HOST_REQUEST_IF_GETLAYER:
    {
        int parent = tree ? rs_cs2_parent_component_id(tree, request->u.if_get_layer.component_id)
                          : -1;
        return CS2VMX_PushInt(vm, parent >= 0 ? parent : -1);
    }

    case CS2VM_HOST_REQUEST_IF_GETTOP:
        return CS2VMX_PushInt(vm, host->client_type);

    case CS2VM_HOST_REQUEST_IF_GETSCROLLX:
    {
        int sx = 0, sy = 0;
        uitree_scroll_get_pos(&scroll, request->u.if_get_scroll_x.component_id, &sx, &sy);
        return CS2VMX_PushInt(vm, sx);
    }

    case CS2VM_HOST_REQUEST_IF_GETSCROLLY:
    {
        int sx = 0, sy = 0;
        uitree_scroll_get_pos(&scroll, request->u.if_get_scroll_y.component_id, &sx, &sy);
        return CS2VMX_PushInt(vm, sy);
    }

    case CS2VM_HOST_REQUEST_IF_GETSCROLLHEIGHT:
        node = rs_cs2_node(host, request->u.if_get_scroll_height.component_id);
        return CS2VMX_PushInt(
            vm, (node && node->type == UIELEM_RS_LAYER) ? node->u.rs_layer.scroll_height : 0);

    case CS2VM_HOST_REQUEST_IF_GETSCROLLWIDTH:
        node = rs_cs2_node(host, request->u.if_getscrollwidth.component_id);
        return CS2VMX_PushInt(
            vm, (node && node->type == UIELEM_RS_LAYER) ? node->u.rs_layer.scroll_width : 0);

    case CS2VM_HOST_REQUEST_IF_GETHIDE:
        node = rs_cs2_node(host, request->u.if_get_width.component_id);
        return CS2VMX_PushInt(vm, node && node->behavior.hide ? 1 : 0);

    case CS2VM_HOST_REQUEST_IF_GETTEXT:
    {
        char buf[512];
        buf[0] = '\0';
        if( tree )
            (void)uitree_get_text(
                tree, request->u.if_gettext.component_id, -1, buf, (int)sizeof(buf));
        return CS2VMX_PushStr(vm, strdup(buf));
    }

    /* ---- IF / CC mutators ---- */
    case CS2VM_HOST_REQUEST_IF_SETHIDE:
        if( tree )
            (void)uitree_apply_hide(
                tree, request->u.if_set_hide.component_id, request->u.if_set_hide.hidden ? 1 : 0);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETPOSITION:
    case CS2VM_HOST_REQUEST_CC_SETPOSITION:
        if( tree )
            (void)uitree_apply_position_modes(
                tree,
                request->u.cc_set_position.component_id,
                request->u.cc_set_position.x,
                request->u.cc_set_position.y,
                request->u.cc_set_position.xmode,
                request->u.cc_set_position.ymode);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETSIZE:
    case CS2VM_HOST_REQUEST_CC_SETSIZE:
        if( tree )
            (void)uitree_apply_size_modes(
                tree,
                request->u.cc_set_size.component_id,
                request->u.cc_set_size.width,
                request->u.cc_set_size.height,
                request->u.cc_set_size.wmode,
                request->u.cc_set_size.hmode);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETSCROLLPOS:
    case CS2VM_HOST_REQUEST_CC_SETSCROLLPOS:
    {
        int cid = request->u.if_set_scroll_pos.component_id;
        int sx = request->u.if_set_scroll_pos.scroll_x;
        int sy = request->u.if_set_scroll_pos.scroll_y;
        node = rs_cs2_node(host, cid);
        if( node && node->type == UIELEM_RS_LAYER )
        {
            int max_x = uitree_scroll_max_x(node);
            int max_y = uitree_scroll_max_y(node);
            if( sx < 0 )
                sx = 0;
            if( sx > max_x )
                sx = max_x;
            if( sy < 0 )
                sy = 0;
            if( sy > max_y )
                sy = max_y;
            uitree_scroll_set_pos(&scroll, cid, sx, sy);
        }
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_IF_SETSCROLLSIZE:
    case CS2VM_HOST_REQUEST_CC_SETSCROLLSIZE:
        if( tree )
            (void)uitree_apply_scroll_size(
                tree,
                request->u.if_set_scroll_size.component_id,
                request->u.if_set_scroll_size.scroll_width,
                request->u.if_set_scroll_size.scroll_height);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETGRAPHIC:
    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC:
        return exec_set_graphic(host, vm, request->u.cc_set_graphic);

    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC2:
        node = rs_cs2_node(host, request->u.cc_set_graphic2.component_id);
        if( node && node->type == UIELEM_RS_GRAPHIC )
        {
            node->u.rs_graphic.scene_id_active = request->u.cc_set_graphic2.graphic_id;
            uitree_mark_node_dirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_graphic2.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETTEXT:
    case CS2VM_HOST_REQUEST_CC_SETTEXT:
        if( tree )
            (void)uitree_apply_text(
                tree, request->u.cc_set_text.component_id, request->u.cc_set_text.text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETOUTLINE:
        if( tree )
            (void)uitree_apply_graphic_outline(
                tree, request->u.if_set_outline.component_id, request->u.if_set_outline.outline);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETOUTLINE:
        if( tree )
            (void)uitree_apply_graphic_outline(
                tree, request->u.cc_set_outline.component_id, request->u.cc_set_outline.outline);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTILING:
        if( tree )
            (void)uitree_apply_graphic_tiled(
                tree, request->u.cc_set_tiling.component_id, request->u.cc_set_tiling.tiling);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETGRAPHICSHADOW:
        if( tree )
            (void)uitree_apply_graphic_shadow(
                tree,
                request->u.cc_set_graphic_shadow.component_id,
                request->u.cc_set_graphic_shadow.shadow);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETCOLOUR:
        if( tree )
            (void)uitree_apply_colour(
                tree, request->u.cc_set_colour.component_id, request->u.cc_set_colour.colour);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETFILL:
        node = rs_cs2_node(host, request->u.cc_set_fill.component_id);
        if( node && node->type == UIELEM_RS_RECT )
        {
            node->u.rs_rect.filled = request->u.cc_set_fill.filled ? 1 : 0;
            uitree_mark_node_dirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_fill.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTRANS:
        node = rs_cs2_node(host, request->u.cc_set_trans.component_id);
        if( node )
        {
            node->trans = request->u.cc_set_trans.trans;
            uitree_mark_node_dirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_trans.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETNOCLICKTHROUGH:
        node = rs_cs2_node(host, request->u.cc_set_no_click_through.component_id);
        if( node )
        {
            node->no_click_through = request->u.cc_set_no_click_through.enabled ? 1 : 0;
            uitree_mark_node_dirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_no_click_through.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTEXTFONT:
        return exec_set_text_font(host, vm, request->u.cc_set_text_font);

    case CS2VM_HOST_REQUEST_CC_SETTEXTALIGN:
        node = rs_cs2_node(host, request->u.cc_set_text_align.component_id);
        if( node && node->type == UIELEM_RS_TEXT )
        {
            node->u.rs_text.center = request->u.cc_set_text_align.x_align;
            node->u.rs_text.y_align = request->u.cc_set_text_align.y_align;
            node->u.rs_text.line_height = request->u.cc_set_text_align.line_height;
            uitree_mark_node_dirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_text_align.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW:
        node = rs_cs2_node(host, request->u.cc_set_text_shadow.component_id);
        if( node && node->type == UIELEM_RS_TEXT )
        {
            node->u.rs_text.shadowed = request->u.cc_set_text_shadow.shadowed ? 1 : 0;
            uitree_mark_node_dirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_text_shadow.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLE:
        node = rs_cs2_node(host, request->u.cc_set_draggable.component_id);
        if( node )
        {
            node->draggable = 1;
            (void)request->u.cc_set_draggable.parent_uid;
            (void)request->u.cc_set_draggable.child_index;
            uitree_mark_node_dirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_draggable.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLEBEHAVIOR:
        node = rs_cs2_node(host, request->u.cc_set_draggable_behavior.component_id);
        if( node )
        {
            node->drag_behavior = request->u.cc_set_draggable_behavior.behavior;
            uitree_mark_node_dirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_draggable_behavior.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADZONE:
        node = rs_cs2_node(host, request->u.cc_set_drag_dead_zone.component_id);
        if( node )
        {
            node->drag_dead_zone = (uint8_t)request->u.cc_set_drag_dead_zone.zone;
            uitree_mark_node_dirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_drag_dead_zone.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADTIME:
        node = rs_cs2_node(host, request->u.cc_set_drag_dead_time.component_id);
        if( node )
        {
            node->drag_dead_time = (uint8_t)request->u.cc_set_drag_dead_time.time;
            uitree_mark_node_dirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_drag_dead_time.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETOBJECT:
        return exec_set_object(
            host,
            vm,
            request->u.if_set_object.component_id,
            request->u.if_set_object.obj_id,
            request->u.if_set_object.count);

    case CS2VM_HOST_REQUEST_CC_SETOBJECT:
        return exec_set_object(
            host,
            vm,
            request->u.cc_set_object.component_id,
            request->u.cc_set_object.obj_id,
            request->u.cc_set_object.count);

    case CS2VM_HOST_REQUEST_CC_DELETEALL:
    {
        int32_t parent_idx =
            tree ? uitree_find_by_component_id(tree, request->u.cc_delete_all.component_id) : -1;
        if( parent_idx >= 0 )
            uitree_cc_delete_all(tree, parent_idx);
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_CC_CREATE:
        return exec_cc_create(host, vm, request->u.cc_create);

    case CS2VM_HOST_REQUEST_CC_FIND:
        return exec_cc_find(host, vm, request->u.cc_find);

    case CS2VM_HOST_REQUEST_IF_FIND:
        return exec_if_find(host, vm, request->u.if_find);

    case CS2VM_HOST_REQUEST_CC_FINDROOT:
    {
        int found = 0;
        int parent =
            tree ? rs_cs2_parent_component_id(tree, request->u.cc_findroot.component_id) : -1;
        if( parent >= 0 )
        {
            rs_cs2_set_cc_target(vm, request->u.cc_findroot.dot_operand, parent);
            found = 1;
        }
        return CS2VMX_PushInt(vm, found);
    }

    case CS2VM_HOST_REQUEST_CC_CHILDREN_FIND:
        return exec_children_find(
            host,
            vm,
            request->u.cc_children_find.parent_id,
            request->u.cc_children_find.start_index,
            0,
            0,
            CS2VM_HOST_REQUEST_CC_CHILDREN_FIND);

    case CS2VM_HOST_REQUEST_IF_CHILDREN_FIND:
        return exec_children_find(
            host,
            vm,
            request->u.if_children_find.uid,
            request->u.if_children_find.start_index,
            1,
            request->u.if_children_find.dot_operand,
            CS2VM_HOST_REQUEST_IF_CHILDREN_FIND);

    case CS2VM_HOST_REQUEST_CC_RESOLVE_PARENT:
    {
        int parent =
            tree ? rs_cs2_parent_component_id(tree, request->u.cc_resolve_parent.component_id)
                 : -1;
        if( parent < 0 )
            return CS2VM_EXECNO_ERROR;
        return CS2VMX_PushInt(vm, parent);
    }

    case CS2VM_HOST_REQUEST_CC_GETID:
        node = rs_cs2_node(host, request->u.cc_get_id.component_id);
        assert(node);

        return CS2VMX_PushInt(vm, node->dynamic ? node->dynamic_child_index : -1);

    case CS2VM_HOST_REQUEST_CC_GETX:
        node = rs_cs2_node(host, request->u.cc_get_id.component_id);
        return CS2VMX_PushInt(vm, node ? node->position.abs_x : 0);

    case CS2VM_HOST_REQUEST_CC_GETY:
        node = rs_cs2_node(host, request->u.cc_get_id.component_id);
        return CS2VMX_PushInt(vm, node ? node->position.abs_y : 0);

    case CS2VM_HOST_REQUEST_CC_GETWIDTH:
        return CS2VMX_PushInt(
            vm, tree ? uitree_get_layout_width(tree, request->u.cc_get_id.component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETHEIGHT:
        return CS2VMX_PushInt(
            vm, tree ? uitree_get_layout_height(tree, request->u.cc_get_id.component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETHIDE:
        node = rs_cs2_node(host, request->u.cc_get_id.component_id);
        return CS2VMX_PushInt(vm, node && node->behavior.hide ? 1 : 0);

    case CS2VM_HOST_REQUEST_CC_GETTEXT:
    {
        char buf[512];
        buf[0] = '\0';
        if( tree )
            (void)uitree_get_text(
                tree, request->u.cc_gettext.component_id, -1, buf, (int)sizeof(buf));
        return CS2VMX_PushStr(vm, strdup(buf));
    }

    case CS2VM_HOST_REQUEST_CC_GETTRANS:
        node = rs_cs2_node(host, request->u.cc_gettrans.component_id);
        return CS2VMX_PushInt(vm, node ? node->trans : 0);

    /* ---- Ops ---- */
    case CS2VM_HOST_REQUEST_CC_SETOP:
    case CS2VM_HOST_REQUEST_IF_SETOP:
        if( tree )
            rs_cs2_apply_op(
                tree,
                request->u.if_set_op.component_id,
                request->u.if_set_op.index,
                request->u.if_set_op.text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETOPBASE:
        if( tree )
            (void)uitree_apply_op_base(
                tree, request->u.if_set_op_base.component_id, request->u.if_set_op_base.text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETOPSUBMENU:
        if( tree )
            (void)uitree_apply_op_submenu(
                tree,
                request->u.if_set_op_submenu.component_id,
                -1,
                request->u.if_set_op_submenu.op_index,
                request->u.if_set_op_submenu.sub_index,
                request->u.if_set_op_submenu.text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETTARGETPRIORITY:
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_CLEAROPS:
        if( tree )
            (void)uitree_clear_ops(tree, request->u.if_clear_ops.component_id, -1);
        return CS2VM_EXECNO_OK;

    /* ---- SetOn (hooks / no-ops) ---- */
    case CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT:
        return exec_set_on_var_transmit(host, &request->u.if_set_on_var_transmit);

    case CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT:
        return exec_set_on_inv_transmit(host, &request->u.if_set_on_inv_transmit);

    case CS2VM_HOST_REQUEST_IF_SETONOP:
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER:
    case CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE:
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT:
    case CS2VM_HOST_REQUEST_IF_SETONTIMER:
    case CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL:
    case CS2VM_HOST_REQUEST_IF_SETONKEY:
    case CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONCLICK:
    case CS2VM_HOST_REQUEST_CC_SETONHOLD:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEREPEAT:
    case CS2VM_HOST_REQUEST_CC_SETONDRAG:
    case CS2VM_HOST_REQUEST_CC_SETONSCROLLWHEEL:
    case CS2VM_HOST_REQUEST_CC_SETONKEY:
    case CS2VM_HOST_REQUEST_CC_SETONOP:
    case CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE:
        return CS2VM_EXECNO_OK;

    /* ---- Widget extras ---- */
    case CS2VM_HOST_REQUEST_WIDGET_SET_INT:
        return exec_widget_set_int(host, vm, request->u.widget_set_int);

    case CS2VM_HOST_REQUEST_WIDGET_SET_INT2:
        /* No UITree fields for paired int setters yet. */
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL:
        return exec_widget_set_model(host, vm, request->u.widget_set_model);

    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_ANGLE:
        return exec_widget_set_model_angle(host, vm, request->u.widget_set_model_angle);

    case CS2VM_HOST_REQUEST_WIDGET_SET_ARC:
        /* UITree has no arc fields yet. */
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND:
        return exec_widget_set_model_kind(host, vm, request->u.widget_set_model_kind);

    case CS2VM_HOST_REQUEST_WIDGET_INPUT_INT:
        /* Input widget fields not on UITree yet. */
        return CS2VM_EXECNO_OK;

    default:
        fprintf(
            stderr,
            "GameRunescape_CS2HostExec: UNHANDLED request kind %d\n",
            (int)request->kind);
        return CS2VM_EXECNO_ERROR;
    }
}
