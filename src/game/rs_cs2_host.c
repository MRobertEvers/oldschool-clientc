#include "game/rs_cs2_host.h"

#include "cs2vm2/cs2vm2.h"
#include "engine/cache_provider.h"
#include "engine/torirs_types.h"
#include "engine/uitree_scene_bridge.h"
#include "inv/inv_manager.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"
#include "ui/uitree_scroll.h"
#include "varp/varp_manager.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UITREE_CLICK_DEBUG
#define UITREE_CLICK_DEBUG 0
#endif

/* =========================================================================
 * Helpers
 * ========================================================================= */

static struct UITree*
rs_cs2_tree(struct RS_CS2Host* host)
{
    assert(host);
    assert(host->tree);
    return host->tree;
}

static struct CacheProvider*
rs_cs2_provider(struct RS_CS2Host* host)
{
    assert(host);
    return host->provider;
}

static int32_t
rs_cs2_find_node(
    struct RS_CS2Host* host,
    int component_id)
{
    struct UITree* tree = rs_cs2_tree(host);
    if( component_id < 0 )
        return -1;
    return UITree_FindByComponentId(tree, component_id);
}

static struct UITreeComponent*
rs_cs2_node(
    struct RS_CS2Host* host,
    int component_id)
{
    struct UITree* tree = rs_cs2_tree(host);
    int32_t idx = rs_cs2_find_node(host, component_id);
    if( idx < 0 )
        return NULL;
    return &tree->components[idx];
}

static int
rs_cs2_yield(
    struct RS_CS2Host* host,
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
    struct RS_CS2Host* host,
    int component_id,
    struct CS2VM_HostRequest const* request)
{
    int group_id;
    struct CacheProvider* provider;

    assert(host);
    assert(request);

    group_id = (component_id >> 16) & 0xffff;
    if( component_id <= 0 || group_id <= 0 )
        return CS2VM_EXECNO_OK;

    if( rs_cs2_find_node(host, component_id) >= 0 )
        return CS2VM_EXECNO_OK;

    provider = rs_cs2_provider(host);
    (void)provider;
    /* Component may already be cached but not baked into the tree yet — still yield. */
    return rs_cs2_yield(host, request);
}

static bool
rs_cs2_sprite_ready(
    struct RS_CS2Host* host,
    int graphic_id)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    /* Negative or absurd ids are not cache sprites — treat as ready (no-op load). */
    if( graphic_id < 0 || graphic_id >= 1000000 )
        return true;
    return provider && CacheProvider_SpriteHas(provider, graphic_id);
}

static bool
rs_cs2_font_ready(
    struct RS_CS2Host* host,
    int font_id)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    if( font_id < 0 )
        return true;
    return provider && CacheProvider_FontHas(provider, font_id);
}

static bool
rs_cs2_model_ready(
    struct RS_CS2Host* host,
    int model_id)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    if( model_id < 0 )
        return true;
    return provider && CacheProvider_ModelHas(provider, model_id);
}

static bool
rs_cs2_resolve_obj_icon(
    struct RS_CS2Host* host,
    int obj_id,
    int* out_scene_id,
    int* out_atlas_index)
{
    int c;
    if( out_scene_id )
        *out_scene_id = -1;
    if( out_atlas_index )
        *out_atlas_index = 0;
    assert(host);
    if( !host->invs || obj_id <= 0 )
        return false;

    for( c = 0; c < host->invs->container_count; c++ )
    {
        struct InvContainer const* container = &host->invs->containers[c];
        int slot;
        for( slot = 0; slot < container->slot_count; slot++ )
        {
            if( container->slots[slot].obj_id != obj_id )
                continue;
            if( container->slots[slot].scene_id < 0 )
                continue;
            if( out_scene_id )
                *out_scene_id = container->slots[slot].scene_id;
            if( out_atlas_index )
                *out_atlas_index = container->slots[slot].atlas_index;
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
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return;
    strncpy(
        tree->components[idx].menu_options.ops[index - 1],
        text ? text : "",
        UITREE_MENU_OPTION_LEN - 1);
    tree->components[idx].menu_options.ops[index - 1][UITREE_MENU_OPTION_LEN - 1] = '\0';
    UITree_MarkNodeDirty(tree, idx);
}

static void
rs_cs2_clear_ops(
    struct UITree* tree,
    int component_id)
{
    int32_t idx;
    int i;
    assert(tree);
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return;
    for( i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
        tree->components[idx].menu_options.ops[i][0] = '\0';
    tree->components[idx].menu_options.option[0] = '\0';
    UITree_MarkNodeDirty(tree, idx);
}

static void
rs_cs2_apply_op_submenu(
    struct UITree* tree,
    int component_id,
    int op_index,
    int sub_index,
    char const* text)
{
    int32_t idx;
    assert(tree);
    if( op_index < 0 || op_index >= UITREE_SUBMENU_OP_SLOTS )
        return;
    if( sub_index < 0 || sub_index >= UITREE_SUBMENU_ENTRY_SLOTS )
        return;
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return;
    strncpy(
        tree->components[idx].menu_options.submenus.ops[op_index][sub_index],
        text ? text : "",
        UITREE_MENU_OPTION_LEN - 1);
    tree->components[idx].menu_options.submenus.ops[op_index][sub_index][UITREE_MENU_OPTION_LEN - 1] =
        '\0';
    UITree_MarkNodeDirty(tree, idx);
}

static void
rs_cs2_get_text(
    struct UITree* tree,
    int component_id,
    char* buf,
    int buf_len)
{
    int32_t idx;
    assert(tree);
    assert(buf);
    assert(buf_len > 0);
    buf[0] = '\0';
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return;
    if( tree->components[idx].type == UIELEM_RS_TEXT && tree->components[idx].u.rs_text.text )
    {
        strncpy(buf, tree->components[idx].u.rs_text.text, (size_t)buf_len - 1);
        buf[buf_len - 1] = '\0';
    }
}

static void
rs_cs2_set_cc_target(
    struct CS2VM2_Thread* thread,
    int dot_operand,
    int component_id)
{
    CS2VM2_SetTargetComponentId(thread, dot_operand, component_id);
}

static int
rs_cs2_parent_component_id(
    struct UITree* tree,
    int component_id)
{
    int32_t idx;
    int32_t parent;
    assert(tree);

    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return -1;
    parent = tree->components[idx].parent;
    if( parent < 0 || (uint32_t)parent >= tree->component_count )
        return -1;
    return tree->components[parent].component_id;
}

static int
rs_cs2_font_wrap_line_count(
    struct ToriRS_Font const* font,
    char const* text,
    int max_width)
{
    int lines = 1;
    int line_w = 0;
    unsigned char c;
    assert(font);
    if( !text || !text[0] )
        return 0;
    if( max_width <= 0 )
        return 1;
    while( (c = (unsigned char)*text++) != 0 )
    {
        int adv;
        if( c == '\n' )
        {
            lines++;
            line_w = 0;
            continue;
        }
        adv = font->draw_width[c];
        if( line_w + adv > max_width && line_w > 0 )
        {
            lines++;
            line_w = adv;
        }
        else
            line_w += adv;
    }
    return lines;
}

static int
rs_cs2_font_wrap_max_line_width(
    struct ToriRS_Font const* font,
    char const* text,
    int max_width)
{
    int best = 0;
    int line_w = 0;
    unsigned char c;
    assert(font);
    if( !text || !text[0] )
        return 0;
    while( (c = (unsigned char)*text++) != 0 )
    {
        int adv;
        if( c == '\n' )
        {
            if( line_w > best )
                best = line_w;
            line_w = 0;
            continue;
        }
        adv = font->draw_width[c];
        if( max_width > 0 && line_w + adv > max_width && line_w > 0 )
        {
            if( line_w > best )
                best = line_w;
            line_w = adv;
        }
        else
            line_w += adv;
    }
    if( line_w > best )
        best = line_w;
    if( max_width > 0 && best > max_width )
        best = max_width;
    return best;
}

static int
rs_cs2_enum_lookup_int(
    struct ToriRS_Enum const* e,
    int key)
{
    int i;
    assert(e);
    if( e->output_is_string )
        return -1;
    if( !e->keys || e->count <= 0 )
        return e->default_int;
    for( i = 0; i < e->count; i++ )
    {
        if( e->keys[i] == key )
            return e->int_values ? e->int_values[i] : e->default_int;
    }
    return e->default_int;
}

static char const*
rs_cs2_enum_lookup_string(
    struct ToriRS_Enum const* e,
    int key)
{
    int i;
    assert(e);
    if( !e->output_is_string || !e->keys )
        return NULL;
    if( e->count <= 0 )
        return e->default_string;
    for( i = 0; i < e->count; i++ )
    {
        if( e->keys[i] == key )
            return e->string_values ? e->string_values[i] : e->default_string;
    }
    return e->default_string;
}

static bool
rs_cs2_obj_param_lookup(
    struct ToriRS_Objtype const* obj,
    int param_id,
    bool* out_is_string,
    int* out_int,
    char const** out_str)
{
    int i;
    if( out_is_string )
        *out_is_string = false;
    if( out_int )
        *out_int = 0;
    if( out_str )
        *out_str = NULL;
    assert(obj);
    if( param_id < 0 || !obj->params || obj->param_count <= 0 )
        return false;
    for( i = 0; i < obj->param_count; i++ )
    {
        if( obj->params[i].key != param_id )
            continue;
        if( obj->params[i].string_value )
        {
            if( out_is_string )
                *out_is_string = true;
            if( out_str )
                *out_str = obj->params[i].string_value;
            return true;
        }
        if( out_int )
            *out_int = obj->params[i].int_value;
        return true;
    }
    return false;
}

static bool
rs_cs2_struct_param_lookup(
    struct ToriRS_Struct const* s,
    int param_id,
    bool* out_is_string,
    int* out_int,
    char const** out_str)
{
    int i;
    if( out_is_string )
        *out_is_string = false;
    if( out_int )
        *out_int = 0;
    if( out_str )
        *out_str = NULL;
    assert(s);
    if( param_id < 0 || !s->params || s->param_count <= 0 )
        return false;
    for( i = 0; i < s->param_count; i++ )
    {
        if( s->params[i].key != param_id )
            continue;
        if( s->params[i].string_value )
        {
            if( out_is_string )
                *out_is_string = true;
            if( out_str )
                *out_str = s->params[i].string_value;
            return true;
        }
        if( out_int )
            *out_int = s->params[i].int_value;
        return true;
    }
    return false;
}

/* =========================================================================
 * Init / Tick
 * ========================================================================= */

void
RS_CS2Host_Init(
    struct RS_CS2Host* host,
    struct UITree* tree,
    struct CacheProvider* provider,
    struct InvManager* invs,
    struct VarPManager* varps)
{
    assert(host);
    assert(tree);
    assert(provider);
    assert(invs);

    memset(host, 0, sizeof(*host));
    host->tree = tree;
    host->provider = provider;
    host->invs = invs;
    host->varps = varps;
    host->client_clock = 100;
    host->client_type = 80;
    host->viewport_w = 765;
    host->viewport_h = 503;
    host->bridge = NULL;
    host->sprite_yield_id = -1;
    host->font_yield_id = -1;
    host->setobject_yield_obj_id = -1;
    /* Serials start at 1 so fresh hooks (last_seen_serial=0) fire once on the
     * first dispatch after registration (widget-loaded parity). */
    host->var_change_serial = 1;
    host->inv_change_serial = 1;
}

void
RS_CS2Host_SetBridge(
    struct RS_CS2Host* host,
    struct UITreeSceneBridge* bridge)
{
    assert(host);
    host->bridge = bridge;
}

void
RS_CS2Host_Tick(struct RS_CS2Host* host)
{
    assert(host);
    host->client_clock++;
}

/* =========================================================================
 * Inventory
 * ========================================================================= */

static int
rs_cs2_inv_size(struct RS_CS2Host* host, int inv_id)
{
    assert(host);
    assert(host->invs);
    if( inv_id < 0 )
        return 0;
    return InvManager_Size(host->invs, inv_id);
}

static int
rs_cs2_inv_get_obj(struct RS_CS2Host* host, int inv_id, int slot)
{
    int obj;
    assert(host);
    assert(host->invs);
    if( inv_id < 0 || slot < 0 )
        return -1;
    obj = InvManager_GetObj(host->invs, inv_id, slot);
    /* Scripts expect -1 for empty (reference INV_GETOBJ pushes -1 when the
     * inv or slot has no item); InvManager's empty sentinel is 0. */
    if( obj <= INV_MANAGER_EMPTY_OBJ_ID )
        return -1;
    return obj;
}

static int
rs_cs2_inv_get_num(struct RS_CS2Host* host, int inv_id, int slot)
{
    assert(host);
    assert(host->invs);
    if( inv_id < 0 || slot < 0 )
        return 0;
    return InvManager_GetNum(host->invs, inv_id, slot);
}

static int
rs_cs2_inv_total(struct RS_CS2Host* host, int inv_id, int item_id)
{
    assert(host);
    assert(host->invs);
    if( inv_id < 0 || item_id <= 0 )
        return 0;
    return InvManager_Total(host->invs, inv_id, item_id);
}

/* =========================================================================
 * Exec handlers
 * ========================================================================= */

static int
exec_push_script(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int script_id)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct CS2VM2_Script* script = NULL;

    if( provider )
        script = CacheProvider_ClientScriptGet(provider, script_id);
    if( !script )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_PUSHSCRIPT;
        req.u.push_script.script_id = script_id;
        return rs_cs2_yield(host, &req);
    }
    return CS2VM2_PushCallScript(thread, script);
}


static int
exec_para_height(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_ParaHeight request,
    int is_width)
{
    int result = 0;
    char const* text = request.text ? request.text : "";
    if( text[0] != '\0' )
    {
        struct ToriRS_Font* font =
            rs_cs2_provider(host) ? CacheProvider_FontGet(rs_cs2_provider(host), request.font_id)
                                  : NULL;
        if( !font )
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = is_width ? CS2VM_HOST_REQUEST_PARAWIDTH : CS2VM_HOST_REQUEST_PARAHEIGHT;
            req.u.para_height = request;
            return rs_cs2_yield(host, &req);
        }
        result = is_width ? rs_cs2_font_wrap_max_line_width(font, text, request.max_width)
                          : rs_cs2_font_wrap_line_count(font, text, request.max_width);
    }
    return CS2VM2_PushInt(thread, result);
}


static int
exec_vars_read_varp(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int varp_id)
{
    int value = 0;
    if( host->varps )
        value = VarPManager_GetVarp(host->varps, varp_id);
    return CS2VM2_PushInt(thread, value);
}


static int
exec_vars_read_varbit(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int varbit_id)
{
    int value = 0;
    if( host->varps )
        value = VarPManager_GetVarbit(host->varps, varbit_id);
    return CS2VM2_PushInt(thread, value);
}


static int
exec_enum_lookup(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_EnumLookup request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Enum* e =
        provider ? CacheProvider_EnumGet(provider, request.enum_id) : NULL;
    if( !e )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_ENUM_LOOKUP;
        req.u.enum_lookup = request;
        return rs_cs2_yield(host, &req);
    }

    (void)request.input_type;
    if( request.output_type == (int)'s' || e->output_is_string )
    {
        char const* value = rs_cs2_enum_lookup_string(e, request.key);
        return CS2VM2_PushStr(thread, strdup(value ? value : "null"));
    }

    return CS2VM2_PushInt(thread, rs_cs2_enum_lookup_int(e, request.key));
}


static int
exec_enum_output_count(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_EnumGetOutputCount request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Enum* e =
        provider ? CacheProvider_EnumGet(provider, request.enum_id) : NULL;
    if( !e )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT;
        req.u.enum_get_output_count = request;
        return rs_cs2_yield(host, &req);
    }
    return CS2VM2_PushInt(thread, e->count);
}


static int
exec_struct_param(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_StructParam request)
{
    bool is_string = false;
    int intval = 0;
    char const* strval = NULL;
    bool found;
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Struct* s =
        provider ? CacheProvider_StructGet(provider, request.struct_id) : NULL;

    if( !s )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_STRUCT_PARAM;
        req.u.struct_param = request;
        return rs_cs2_yield(host, &req);
    }

    found = rs_cs2_struct_param_lookup(s, request.param_id, &is_string, &intval, &strval);
    if( found && is_string )
        return CS2VM2_PushStr(thread, strdup(strval ? strval : ""));
    if( found )
        return CS2VM2_PushInt(thread, intval);
    return CS2VM2_PushInt(thread, 0);
}

static int
exec_oc_param(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Param request)
{
    bool is_string = false;
    int intval = 0;
    char const* strval = NULL;
    bool found;
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Objtype* obj =
        provider ? CacheProvider_ObjtypeGet(provider, request.item_id) : NULL;
    struct ToriRS_ParamType* param =
        provider ? CacheProvider_ParamGet(provider, request.param_id) : NULL;

    /* item -1 (empty slot) is a valid script input: never yield for it — the
     * yield planner requires a loadable id — just push the param default. */
    if( !obj && request.item_id >= 0 )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_OC_PARAM;
        req.u.oc_param = request;
        return rs_cs2_yield(host, &req);
    }

    /* The ParamType config decides string-vs-int and supplies the default the
     * obj may not carry. Load it before deciding what to push. */
    if( !param && request.param_id >= 0 )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_PARAM_TYPE;
        req.u.oc_param = request;
        return rs_cs2_yield(host, &req);
    }

    found = obj && rs_cs2_obj_param_lookup(obj, request.param_id, &is_string, &intval, &strval);
    if( param && param->is_string )
    {
        if( found && strval )
            return CS2VM2_PushStr(thread, strdup(strval));
        return CS2VM2_PushStr(
            thread, strdup(param->default_string ? param->default_string : ""));
    }
    if( found )
        return CS2VM2_PushInt(thread, intval);
    return CS2VM2_PushInt(thread, param ? param->default_int : 0);
}


static int
exec_oc_int_param(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_IntParam request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Objtype* obj =
        provider ? CacheProvider_ObjtypeGet(provider, request.item_id) : NULL;
    int value = 0;

    if( request.item_id < 0 )
        return CS2VM2_PushInt(thread, 0);

    if( !obj )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_OC_INT_PARAM;
        req.u.oc_int_param = request;
        return rs_cs2_yield(host, &req);
    }

    switch( request.field )
    {
    case CS2VM_OC_INT_COST:
        /* Stub: cost not carried on ToriRS_Objtype yet. */
        value = 0;
        break;
    case CS2VM_OC_INT_STACKABLE:
        value = obj->stackable;
        break;
    case CS2VM_OC_INT_MEMBERS:
        /* Stub: members flag not on ToriRS_Objtype yet. */
        value = 0;
        break;
    case CS2VM_OC_INT_ID:
        value = obj->id;
        break;
    }
    return CS2VM2_PushInt(thread, value);
}


static int
exec_oc_name(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Name request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Objtype* obj =
        provider ? CacheProvider_ObjtypeGet(provider, request.item_id) : NULL;
    char const* name = "null";

    if( request.item_id < 0 )
        return CS2VM2_PushStr(thread, strdup(name));

    if( !obj )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_OC_NAME;
        req.u.oc_name = request;
        return rs_cs2_yield(host, &req);
    }

    if( obj->name[0] != '\0' )
        name = obj->name;
    return CS2VM2_PushStr(thread, strdup(name));
}


static int
exec_oc_unplaceholder(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Unplaceholder request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);

    /* item -1 (empty slot) is a valid script input: never yield for it — the
     * yield planner requires a loadable id — and there is nothing to resolve,
     * so pass the id straight through. */
    if( request.item_id < 0 || (provider && CacheProvider_ObjtypeHas(provider, request.item_id)) )
        return CS2VM2_PushInt(thread, request.item_id);

    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER;
        req.u.oc_unplaceholder = request;
        return rs_cs2_yield(host, &req);
    }
}


static int
exec_set_graphic(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_CC_SetGraphic request)
{
    struct UITree* tree = rs_cs2_tree(host);
    (void)thread;

    if( request.graphic_id >= 0 && !rs_cs2_sprite_ready(host, request.graphic_id) )
    {
        /* After a failed SpriteLoad the id is still missing — do not re-yield. */
        if( host->sprite_yield_id == request.graphic_id )
        {
            host->sprite_yield_id = -1;
            (void)UITree_ApplyGraphic(tree, request.component_id, -1, 0);
            return CS2VM_EXECNO_OK;
        }
        host->sprite_yield_id = request.graphic_id;
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = CS2VM_HOST_REQUEST_CC_SETGRAPHIC;
            req.u.cc_set_graphic = request;
            return rs_cs2_yield(host, &req);
        }
    }
    host->sprite_yield_id = -1;

    /* Upload to scene then store scene element id on the node. */
    {
        int scene_id = request.graphic_id;
        if( host->bridge && request.graphic_id >= 0 && request.graphic_id < 1000000 )
            scene_id = UITreeSceneBridge_EnsureSprite(host->bridge, request.graphic_id);
#if UITREE_CLICK_DEBUG
        fprintf(
            stderr,
            "uitree_click: SETGRAPHIC component_id=%d graphic_id=%d scene_id=%d\n",
            request.component_id,
            request.graphic_id,
            scene_id);
#endif
        (void)UITree_ApplyGraphic(tree, request.component_id, scene_id, 0);
    }
    return CS2VM_EXECNO_OK;
}


static int
exec_set_object(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int component_id,
    int obj_id,
    int count)
{
    struct UITree* tree = rs_cs2_tree(host);
    struct CacheProvider* provider = rs_cs2_provider(host);
    int scene_id = -1;
    int atlas_index = 0;
    (void)thread;

    if( obj_id <= 0 )
    {
#if UITREE_CLICK_DEBUG
        fprintf(
            stderr,
            "uitree_click: SETOBJECT component_id=%d obj_id=%d count=%d (clear)\n",
            component_id,
            obj_id,
            count);
#endif
        (void)UITree_ApplyObject(tree, component_id, 0, 0, -1, 0);
        return CS2VM_EXECNO_OK;
    }

    if( !provider || !CacheProvider_ObjtypeHas(provider, obj_id) )
    {
        if( host->setobject_yield_obj_id == obj_id )
        {
            host->setobject_yield_obj_id = -1;
            (void)UITree_ApplyObject(tree, component_id, obj_id, count, -1, 0);
            return CS2VM_EXECNO_OK;
        }
        host->setobject_yield_obj_id = obj_id;
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = CS2VM_HOST_REQUEST_CC_SETOBJECT;
            req.u.cc_set_object.component_id = component_id;
            req.u.cc_set_object.obj_id = obj_id;
            req.u.cc_set_object.count = count;
            return rs_cs2_yield(host, &req);
        }
    }

    if( host->bridge )
    {
        scene_id = UITreeSceneBridge_EnsureObjIcon(host->bridge, obj_id, count);
        if( scene_id < 0 )
        {
            struct ToriRS_Objtype* obj = CacheProvider_ObjtypeGet(provider, obj_id);
            int model_id = obj ? obj->inventory_model_id : -1;
            if( model_id > 0 && !CacheProvider_ModelHas(provider, model_id) )
            {
                if( host->setobject_yield_obj_id == obj_id )
                {
                    host->setobject_yield_obj_id = -1;
                    (void)UITree_ApplyObject(tree, component_id, obj_id, count, -1, 0);
                    return CS2VM_EXECNO_OK;
                }
                host->setobject_yield_obj_id = obj_id;
                {
                    struct CS2VM_HostRequest req = { 0 };
                    req.kind = CS2VM_HOST_REQUEST_CC_SETOBJECT;
                    req.u.cc_set_object.component_id = component_id;
                    req.u.cc_set_object.obj_id = obj_id;
                    req.u.cc_set_object.count = count;
                    return rs_cs2_yield(host, &req);
                }
            }
        }
    }
    else
        (void)rs_cs2_resolve_obj_icon(host, obj_id, &scene_id, &atlas_index);

    host->setobject_yield_obj_id = -1;
#if UITREE_CLICK_DEBUG
    fprintf(
        stderr,
        "uitree_click: SETOBJECT component_id=%d obj_id=%d count=%d scene_id=%d\n",
        component_id,
        obj_id,
        count,
        scene_id);
#endif
    (void)UITree_ApplyObject(tree, component_id, obj_id, count, scene_id, atlas_index);
    return CS2VM_EXECNO_OK;
}


static int
exec_set_text_font(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_CC_SetTextFont request)
{
    (void)thread;

    if( request.font_id >= 0 && !rs_cs2_font_ready(host, request.font_id) )
    {
        if( host->font_yield_id == request.font_id )
        {
            host->font_yield_id = -1;
            (void)UITree_ApplyTextFont(rs_cs2_tree(host), request.component_id, -1);
            return CS2VM_EXECNO_OK;
        }
        host->font_yield_id = request.font_id;
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = CS2VM_HOST_REQUEST_CC_SETTEXTFONT;
            req.u.cc_set_text_font = request;
            return rs_cs2_yield(host, &req);
        }
    }
    host->font_yield_id = -1;

    {
        int font_id = request.font_id;
        if( host->bridge && font_id >= 0 )
            font_id = UITreeSceneBridge_EnsureFont(host->bridge, font_id);
        (void)UITree_ApplyTextFont(rs_cs2_tree(host), request.component_id, font_id);
    }
    return CS2VM_EXECNO_OK;
}


/* CC_COPY clones an existing dynamic child into another slot. The bank tab
 * strip (script 505) builds tab 0 with CC_CREATE then copies it into slots
 * 1..9; without this the whole strip collapses onto the one created tab. */
static int
exec_cc_copy(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_CC_Copy request)
{
    struct UITree* tree = rs_cs2_tree(host);
    int const parent_id = request.parent_id;
    int32_t parent_idx;
    int32_t child_idx;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };

    yield_req.kind = CS2VM_HOST_REQUEST_CC_COPY;
    yield_req.u.cc_copy = request;
    yield_res = rs_cs2_yield_if_group_missing(host, parent_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
        return yield_res;

    assert(tree);

    parent_idx = UITree_FindByComponentId(tree, parent_id);
    if( parent_idx < 0 )
    {
        if( parent_id > 0 )
            return rs_cs2_yield(host, &yield_req);
        return CS2VM_EXECNO_OK;
    }

    child_idx = UITree_CcCopy(
        tree, parent_idx, parent_id, request.src_sub_id, request.dst_sub_id);
    if( child_idx < 0 )
        return CS2VM_EXECNO_ERROR;

    rs_cs2_set_cc_target(vm, request.dot_operand, tree->components[child_idx].component_id);
    return CS2VM_EXECNO_OK;
}

static int
exec_cc_create(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
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

    parent_idx = UITree_FindByComponentId(tree, parent_id);
    if( parent_idx < 0 )
    {
        if( parent_id > 0 )
        {
            /* Group claimed ready earlier but parent still missing — yield again. */
            return rs_cs2_yield(host, &yield_req);
        }
        return CS2VM_EXECNO_OK;
    }

    child_idx = UITree_CcCreate(
        tree, parent_idx, parent_id, request.component_type, request.child_index);
    if( child_idx < 0 )
        return CS2VM_EXECNO_ERROR;

    /* Leave size 0; scripts call CC_SETSIZE when needed. Soft3D uses native
     * sprite size when layout w/h are 0 — do not stretch 32x32 icons to the
     * parent slot (that thickens obj-icon outlines). */

#if UITREE_CLICK_DEBUG
    fprintf(
        stderr,
        "uitree_click: CC_CREATE parent_id=%d child_id=%d type=%d idx=%d size=%dx%d\n",
        parent_id,
        tree->components[child_idx].component_id,
        request.component_type,
        (int)child_idx,
        tree->components[child_idx].position.width,
        tree->components[child_idx].position.height);
#endif

    rs_cs2_set_cc_target(vm, request.dot_operand, tree->components[child_idx].component_id);
    return CS2VM_EXECNO_OK;
}

static int
exec_cc_find(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
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
        parent_idx = UITree_FindByComponentId(tree, request.parent_id);
        if( parent_idx >= 0 )
        {
            int32_t child_idx = UITree_FindChildBySubid(
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

    return CS2VM2_PushInt(vm, found);
}

static int
exec_if_find(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
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

    return CS2VM2_PushInt(vm, found);
}

static int
exec_children_find(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
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

    CS2VM2_ResetChildrenIter(vm);
    if( tree )
    {
        if( UITree_FindByComponentId(tree, parent_id) < 0 && parent_id > 0 )
            return rs_cs2_yield(host, &yield_req);

        vm->children_iter_count = UITree_CollectDynamicChildIndices(
            tree,
            parent_id,
            start_index,
            vm->children_iter_indices,
            CS2VM2_CHILDREN_ITER_MAX);

        if( set_target_dot && UITree_FindByComponentId(tree, parent_id) >= 0 )
            rs_cs2_set_cc_target(vm, dot_operand, parent_id);
    }
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_model(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
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
    {
        int scene_model = request.model_id;
        if( host->bridge && scene_model >= 0 )
            scene_model = UITreeSceneBridge_EnsureModel(host->bridge, scene_model);
        (void)UITree_ApplyModel(rs_cs2_tree(host), request.component_id, scene_model);
    }
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_model_kind(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_WidgetSetModelKind request)
{
    (void)vm;
#if UITREE_CLICK_DEBUG
    fprintf(
        stderr,
        "uitree_click: SET_MODEL_KIND component_id=%d kind=%d model_id=%d\n",
        request.component_id,
        (int)request.model_kind,
        request.model_id);
#endif
    if( request.model_kind == CS2VM_MODEL_KIND_PLAIN && request.model_id >= 0 &&
        !rs_cs2_model_ready(host, request.model_id) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND;
        req.u.widget_set_model_kind = request;
        return rs_cs2_yield(host, &req);
    }
    if( request.model_kind == CS2VM_MODEL_KIND_PLAIN && rs_cs2_tree(host) )
    {
        int scene_model = request.model_id;
        if( host->bridge && scene_model >= 0 )
            scene_model = UITreeSceneBridge_EnsureModel(host->bridge, scene_model);
        (void)UITree_ApplyModel(rs_cs2_tree(host), request.component_id, scene_model);
    }
    /* NPC/player heads: apply model_id when present; full appearance load is task-layer. */
    else if( rs_cs2_tree(host) && request.model_id >= 0 )
    {
        int scene_model = request.model_id;
        if( host->bridge && request.model_kind == CS2VM_MODEL_KIND_PLAIN )
            scene_model = UITreeSceneBridge_EnsureModel(host->bridge, scene_model);
        (void)UITree_ApplyModel(rs_cs2_tree(host), request.component_id, scene_model);
    }
    /* PLAYER_SELF / heads with model_id < 0: no-op OK (clientCode 328 draws separately). */
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_int(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_WidgetSetInt request)
{
    struct UITreeComponent* node = rs_cs2_node(host, request.component_id);
    int32_t idx;
    (void)vm;

    if( !node )
    {
        /* Scripts set properties on other groups (e.g. interface 100's search
         * button targets chatbox 162:36). Yield to sub-mount the group; once
         * its root is baked, a still-missing child is a no-op (reference
         * tolerates sets on absent widgets). */
        int group_id = (request.component_id >> 16) & 0xffff;
        if( request.component_id > 0 && group_id > 0 &&
            rs_cs2_find_node(host, group_id << 16) < 0 )
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = CS2VM_HOST_REQUEST_WIDGET_SET_INT;
            req.u.widget_set_int = request;
            return rs_cs2_yield(host, &req);
        }
        return CS2VM_EXECNO_OK;
    }

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
        (void)UITree_ApplyColour(rs_cs2_tree(host), request.component_id, request.value);
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
        (void)UITree_ApplyClickMask(rs_cs2_tree(host), request.component_id, request.value);
        break;
    case CS2VM_WIDGET_INT_DRAG_DEAD_ZONE:
        node->drag_dead_zone = (uint8_t)request.value;
        break;
    case CS2VM_WIDGET_INT_DRAG_DEAD_TIME:
        node->drag_dead_time = (uint8_t)request.value;
        break;
    case CS2VM_WIDGET_INT_MODEL_TRANSPARENT:
        (void)UITree_ApplyModelTransparent(
            rs_cs2_tree(host), request.component_id, request.value);
        break;
    case CS2VM_WIDGET_INT_MODEL_ANIM:
        /* Sequence id for a model widget. The client tick driver loads the
         * sequence and advances/applies frames to the model. -1 clears. */
        if( node->type == UIELEM_RS_MODEL )
        {
            node->u.rs_model.anim_seq_id = request.value;
            node->u.rs_model.anim_frame = 0;
            node->u.rs_model.anim_frame_cycle = 0;
        }
        break;
    case CS2VM_WIDGET_INT_MODEL_ORTHOG:
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
        UITree_MarkNodeDirty(rs_cs2_tree(host), idx);
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_model_angle(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest_WidgetSetModelAngle request)
{
    (void)vm;
    (void)UITree_ApplyModelAngle(
        rs_cs2_tree(host),
        request.component_id,
        request.angle_x,
        request.angle_y,
        request.zoom);
    return CS2VM_EXECNO_OK;
}

/* Acquire the inv-transmit hook slot for component_id. Re-registration for the
 * same component reuses its entry (the new script supersedes the old) while
 * preserving last_seen_serial — a transmit script re-registering itself must not
 * re-arm and re-fire every pump (TS parity: reassigning node.onInvTransmit does
 * not reset lastChangedInvCount). When the array is full, entries whose
 * component no longer exists are purged first. Returns NULL when genuinely
 * full; the returned slot is zeroed except last_seen_serial. */
static struct RS_CS2InvTransmitHook*
rs_cs2_acquire_inv_transmit_hook(
    struct RS_CS2Host* host,
    int component_id)
{
    int i;
    struct RS_CS2InvTransmitHook* hook;

    for( i = 0; i < host->inv_transmit_hook_count; i++ )
    {
        hook = &host->inv_transmit_hooks[i];
        if( hook->component_id == component_id )
        {
            uint32_t const last_seen = hook->last_seen_serial;
            memset(hook, 0, sizeof(*hook));
            hook->last_seen_serial = last_seen;
            return hook;
        }
    }

    if( host->inv_transmit_hook_count >= RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX )
    {
        int w = 0;
        for( i = 0; i < host->inv_transmit_hook_count; i++ )
        {
            if( UITree_FindByComponentId(host->tree, host->inv_transmit_hooks[i].component_id) <
                0 )
                continue;
            if( w != i )
                host->inv_transmit_hooks[w] = host->inv_transmit_hooks[i];
            w++;
        }
        host->inv_transmit_hook_count = w;
        if( host->inv_transmit_hook_count >= RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX )
            return NULL;
    }

    hook = &host->inv_transmit_hooks[host->inv_transmit_hook_count++];
    memset(hook, 0, sizeof(*hook));
    return hook;
}

/* Var-transmit counterpart of rs_cs2_acquire_inv_transmit_hook. */
static struct RS_CS2VarTransmitHook*
rs_cs2_acquire_var_transmit_hook(
    struct RS_CS2Host* host,
    int component_id)
{
    int i;
    struct RS_CS2VarTransmitHook* hook;

    for( i = 0; i < host->var_transmit_hook_count; i++ )
    {
        hook = &host->var_transmit_hooks[i];
        if( hook->component_id == component_id )
        {
            uint32_t const last_seen = hook->last_seen_serial;
            memset(hook, 0, sizeof(*hook));
            hook->last_seen_serial = last_seen;
            return hook;
        }
    }

    if( host->var_transmit_hook_count >= RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX )
    {
        int w = 0;
        for( i = 0; i < host->var_transmit_hook_count; i++ )
        {
            if( UITree_FindByComponentId(host->tree, host->var_transmit_hooks[i].component_id) <
                0 )
                continue;
            if( w != i )
                host->var_transmit_hooks[w] = host->var_transmit_hooks[i];
            w++;
        }
        host->var_transmit_hook_count = w;
        if( host->var_transmit_hook_count >= RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX )
            return NULL;
    }

    hook = &host->var_transmit_hooks[host->var_transmit_hook_count++];
    memset(hook, 0, sizeof(*hook));
    return hook;
}

/* Copy hook string args (mask + fixed buffers) from a SetOn request. Both
 * request and hook use the CS2VM_SETON_STR_ARG_* layout. */
#define RS_CS2_COPY_HOOK_STR_ARGS(hook, request)                                 \
    do                                                                           \
    {                                                                            \
        (hook)->str_arg_mask = (request)->str_arg_mask;                          \
        (hook)->str_arg_count = (request)->str_arg_count;                        \
        if( (hook)->str_arg_count > CS2VM_SETON_STR_ARG_MAX )                    \
            (hook)->str_arg_count = CS2VM_SETON_STR_ARG_MAX;                     \
        memcpy((hook)->str_args, (request)->str_args, sizeof((hook)->str_args)); \
    } while( 0 )

static int
exec_set_on_inv_transmit(
    struct RS_CS2Host* host,
    struct CS2VM_HostRequest_IF_SetOnInvTransmit const* request)
{
    struct RS_CS2InvTransmitHook* hook;
    assert(host);
    assert(request);
    hook = rs_cs2_acquire_inv_transmit_hook(host, request->component_id);
    if( !hook )
    {
        fprintf(
            stderr,
            "rs_cs2_host: inv_transmit_hooks full (%d), dropping script_id=%d component_id=%d\n",
            RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX,
            request->script_id,
            request->component_id);
        return CS2VM_EXECNO_OK;
    }
    hook->component_id = request->component_id;
    hook->script_id = request->script_id;
    hook->int_arg_count = request->int_arg_count;
    if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
        hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
    memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
    RS_CS2_COPY_HOOK_STR_ARGS(hook, request);
    hook->trigger_count = request->trigger_count;
    if( hook->trigger_count > RS_CS2_HOST_TRANSMIT_TRIGGER_MAX )
        hook->trigger_count = RS_CS2_HOST_TRANSMIT_TRIGGER_MAX;
    if( request->trigger_ids && hook->trigger_count > 0 )
        memcpy(hook->trigger_ids, request->trigger_ids, (size_t)hook->trigger_count * sizeof(int));
#if UITREE_CLICK_DEBUG
    fprintf(
        stderr,
        "uitree_click: SETON IF_SETONINVTRANSMIT component_id=%d script_id=%d argc=%d "
        "triggers=%d",
        request->component_id,
        request->script_id,
        request->int_arg_count,
        request->trigger_count);
    {
        int ti;
        for( ti = 0; ti < hook->trigger_count; ti++ )
            fprintf(stderr, "%s%d", ti == 0 ? " [" : ",", hook->trigger_ids[ti]);
        if( hook->trigger_count > 0 )
            fprintf(stderr, "]");
    }
    fprintf(stderr, "\n");
#endif
    return CS2VM_EXECNO_OK;
}

static int
exec_set_on_var_transmit(
    struct RS_CS2Host* host,
    struct CS2VM_HostRequest_IF_SetOnVarTransmit const* request)
{
    struct RS_CS2VarTransmitHook* hook;
    assert(host);
    hook = rs_cs2_acquire_var_transmit_hook(host, request->component_id);
    if( !hook )
        return CS2VM_EXECNO_OK;
    hook->component_id = request->component_id;
    hook->script_id = request->script_id;
    hook->int_arg_count = request->int_arg_count;
    if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
        hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
    memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
    RS_CS2_COPY_HOOK_STR_ARGS(hook, request);
    hook->trigger_count = request->trigger_count;
    if( hook->trigger_count > RS_CS2_HOST_TRANSMIT_TRIGGER_MAX )
        hook->trigger_count = RS_CS2_HOST_TRANSMIT_TRIGGER_MAX;
    if( request->trigger_ids && hook->trigger_count > 0 )
        memcpy(hook->trigger_ids, request->trigger_ids, (size_t)hook->trigger_count * sizeof(int));
    return CS2VM_EXECNO_OK;
}

/* CC-level transmit hooks: same registration as the IF-level ones, but the
 * component is the VM's active child and args/triggers arrive in the CC
 * request shape. Previously these opcodes were silently discarded, so
 * dynamically-built lists never refreshed on inv/var changes. */
static int
exec_set_on_cc_transmit(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    enum CS2VM_HostRequestKind kind,
    struct CS2VM_HostRequest_CC_SetOnOp const* request)
{
    int component_id;

    assert(host);
    assert(vm);
    if( !request )
        return CS2VM_EXECNO_OK;

    /* Dot vs active register — resolved at op time in the VM (see
     * exec_set_on_cc_event). */
    component_id = request->component_id;
    if( component_id < 0 )
        return CS2VM_EXECNO_OK;

    if( kind == CS2VM_HOST_REQUEST_CC_SETONINVTRANSMIT )
    {
        struct RS_CS2InvTransmitHook* hook;
        hook = rs_cs2_acquire_inv_transmit_hook(host, component_id);
        if( !hook )
        {
            fprintf(
                stderr,
                "rs_cs2_host: inv_transmit_hooks full (%d), dropping cc script_id=%d "
                "component_id=%d\n",
                RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX,
                request->script_id,
                component_id);
            return CS2VM_EXECNO_OK;
        }
        hook->component_id = component_id;
        hook->script_id = request->script_id;
        hook->int_arg_count = request->int_arg_count;
        if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
            hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
        memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
        RS_CS2_COPY_HOOK_STR_ARGS(hook, request);
        hook->trigger_count = request->trigger_count;
        if( hook->trigger_count > RS_CS2_HOST_TRANSMIT_TRIGGER_MAX )
            hook->trigger_count = RS_CS2_HOST_TRANSMIT_TRIGGER_MAX;
        if( request->trigger_ids && hook->trigger_count > 0 )
            memcpy(
                hook->trigger_ids,
                request->trigger_ids,
                (size_t)hook->trigger_count * sizeof(int));
        return CS2VM_EXECNO_OK;
    }

    if( kind == CS2VM_HOST_REQUEST_CC_SETONVARTRANSMIT )
    {
        struct RS_CS2VarTransmitHook* hook;
        hook = rs_cs2_acquire_var_transmit_hook(host, component_id);
        if( !hook )
            return CS2VM_EXECNO_OK;
        hook->component_id = component_id;
        hook->script_id = request->script_id;
        hook->int_arg_count = request->int_arg_count;
        if( hook->int_arg_count > RS_CS2_HOST_TRANSMIT_INT_ARG_MAX )
            hook->int_arg_count = RS_CS2_HOST_TRANSMIT_INT_ARG_MAX;
        memcpy(hook->int_args, request->int_args, sizeof(hook->int_args));
        RS_CS2_COPY_HOOK_STR_ARGS(hook, request);
        hook->trigger_count = request->trigger_count;
        if( hook->trigger_count > RS_CS2_HOST_TRANSMIT_TRIGGER_MAX )
            hook->trigger_count = RS_CS2_HOST_TRANSMIT_TRIGGER_MAX;
        if( request->trigger_ids && hook->trigger_count > 0 )
            memcpy(
                hook->trigger_ids,
                request->trigger_ids,
                (size_t)hook->trigger_count * sizeof(int));
        return CS2VM_EXECNO_OK;
    }

    return CS2VM_EXECNO_OK;
}

static struct UITreeRuntimeScriptHook*
rs_cs2_runtime_hook_slot(
    struct UITreeComponent* node,
    enum CS2VM_HostRequestKind kind)
{
    assert(node);
    switch( kind )
    {
    case CS2VM_HOST_REQUEST_IF_SETONCLICK:
    case CS2VM_HOST_REQUEST_CC_SETONCLICK:
        return &node->runtime_hooks.on_click;
    case CS2VM_HOST_REQUEST_IF_SETONHOLD:
    case CS2VM_HOST_REQUEST_CC_SETONHOLD:
        return &node->runtime_hooks.on_hold;
    case CS2VM_HOST_REQUEST_IF_SETONOP:
    case CS2VM_HOST_REQUEST_CC_SETONOP:
        return &node->runtime_hooks.on_op;
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER:
        return &node->runtime_hooks.on_mouse_over;
    case CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE:
        return &node->runtime_hooks.on_mouse_leave;
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEREPEAT:
        return &node->runtime_hooks.on_mouse_repeat;
    case CS2VM_HOST_REQUEST_IF_SETONTIMER:
    case CS2VM_HOST_REQUEST_CC_SETONTIMER:
        return &node->runtime_hooks.on_timer;
    case CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL:
    case CS2VM_HOST_REQUEST_CC_SETONSCROLLWHEEL:
        return &node->runtime_hooks.on_scroll_wheel;
    case CS2VM_HOST_REQUEST_IF_SETONDRAG:
    case CS2VM_HOST_REQUEST_CC_SETONDRAG:
        return &node->runtime_hooks.on_drag;
    case CS2VM_HOST_REQUEST_IF_SETONDRAGCOMPLETE:
    case CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE:
        return &node->runtime_hooks.on_drag_complete;
    case CS2VM_HOST_REQUEST_IF_SETONRESIZE:
    case CS2VM_HOST_REQUEST_CC_SETONRESIZE:
        return &node->runtime_hooks.on_resize;
    case CS2VM_HOST_REQUEST_IF_SETONSUBCHANGE:
    case CS2VM_HOST_REQUEST_CC_SETONSUBCHANGE:
        return &node->runtime_hooks.on_sub_change;
    default:
        return NULL;
    }
}

#if UITREE_CLICK_DEBUG
static char const*
rs_cs2_seton_kind_str(enum CS2VM_HostRequestKind kind)
{
    switch( kind )
    {
    case CS2VM_HOST_REQUEST_IF_SETONCLICK:
        return "IF_SETONCLICK";
    case CS2VM_HOST_REQUEST_IF_SETONOP:
        return "IF_SETONOP";
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER:
        return "IF_SETONMOUSEOVER";
    case CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE:
        return "IF_SETONMOUSELEAVE";
    case CS2VM_HOST_REQUEST_CC_SETONCLICK:
        return "CC_SETONCLICK";
    case CS2VM_HOST_REQUEST_CC_SETONOP:
        return "CC_SETONOP";
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER:
        return "CC_SETONMOUSEOVER";
    case CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE:
        return "CC_SETONMOUSELEAVE";
    case CS2VM_HOST_REQUEST_IF_SETONDRAG:
        return "IF_SETONDRAG";
    case CS2VM_HOST_REQUEST_IF_SETONDRAGCOMPLETE:
        return "IF_SETONDRAGCOMPLETE";
    case CS2VM_HOST_REQUEST_IF_SETONRESIZE:
        return "IF_SETONRESIZE";
    case CS2VM_HOST_REQUEST_IF_SETONSUBCHANGE:
        return "IF_SETONSUBCHANGE";
    case CS2VM_HOST_REQUEST_CC_SETONDRAG:
        return "CC_SETONDRAG";
    case CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE:
        return "CC_SETONDRAGCOMPLETE";
    case CS2VM_HOST_REQUEST_CC_SETONRESIZE:
        return "CC_SETONRESIZE";
    case CS2VM_HOST_REQUEST_CC_SETONSUBCHANGE:
        return "CC_SETONSUBCHANGE";
    default:
        return "SETON?";
    }
}
#endif

static int
exec_set_on_if_event(
    struct RS_CS2Host* host,
    enum CS2VM_HostRequestKind kind,
    struct CS2VM_HostRequest_IF_SetOnOp const* request)
{
    struct UITree* tree;
    struct UITreeComponent* node;
    struct UITreeRuntimeScriptHook* slot;

    assert(host);
    if( !request )
        return CS2VM_EXECNO_OK;

    tree = rs_cs2_tree(host);
    if( !tree )
        return CS2VM_EXECNO_OK;

    node = rs_cs2_node(host, request->component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    slot = rs_cs2_runtime_hook_slot(node, kind);
    if( !slot )
        return CS2VM_EXECNO_OK;

#if UITREE_CLICK_DEBUG
    fprintf(
        stderr,
        "uitree_click: SETON %s component_id=%d script_id=%d argc=%d\n",
        rs_cs2_seton_kind_str(kind),
        request->component_id,
        request->script_id,
        request->int_arg_count);
#endif

    {
        char const* strp[CS2VM_SETON_STR_ARG_MAX];
        for( int i = 0; i < CS2VM_SETON_STR_ARG_MAX; i++ )
            strp[i] = request->str_args[i];
        (void)UITree_ApplyRuntimeHook(
            tree,
            request->component_id,
            slot,
            request->script_id,
            request->int_arg_count > 0 ? request->int_args : NULL,
            request->int_arg_count,
            request->str_arg_mask,
            strp,
            request->str_arg_count);
    }
    return CS2VM_EXECNO_OK;
}

static int
exec_set_on_cc_event(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* vm,
    enum CS2VM_HostRequestKind kind,
    struct CS2VM_HostRequest_CC_SetOnOp const* request)
{
    struct UITree* tree;
    struct UITreeComponent* node;
    struct UITreeRuntimeScriptHook* slot;
    int component_id;

    assert(host);
    assert(vm);
    if( !request )
        return CS2VM_EXECNO_OK;

    tree = rs_cs2_tree(host);
    if( !tree )
        return CS2VM_EXECNO_OK;

    /* Target resolved at op time in the VM (dot vs active register — the
     * scrollbar/dropdown procs attach handlers to several dot children in a
     * row, so re-reading the active register here binds the wrong child). */
    component_id = request->component_id;
    node = rs_cs2_node(host, component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    slot = rs_cs2_runtime_hook_slot(node, kind);
    if( !slot )
        return CS2VM_EXECNO_OK;

#if UITREE_CLICK_DEBUG
    fprintf(
        stderr,
        "uitree_click: SETON %s component_id=%d script_id=%d argc=%d\n",
        rs_cs2_seton_kind_str(kind),
        component_id,
        request->script_id,
        request->int_arg_count);
#endif

    {
        char const* strp[CS2VM_SETON_STR_ARG_MAX];
        for( int i = 0; i < CS2VM_SETON_STR_ARG_MAX; i++ )
            strp[i] = request->str_args[i];
        (void)UITree_ApplyRuntimeHook(
            tree,
            component_id,
            slot,
            request->script_id,
            request->int_arg_count > 0 ? request->int_args : NULL,
            request->int_arg_count,
            request->str_arg_mask,
            strp,
            request->str_arg_count);
    }
    return CS2VM_EXECNO_OK;
}

/* =========================================================================
 * Main dispatcher
 * ========================================================================= */

int
RS_CS2Host_Exec(
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest* request)
{
    struct RS_CS2Host* host;
    struct UITree* tree;
    struct UITreeComponent* node;

    assert(vm);
    assert(request);

    host = (struct RS_CS2Host*)CS2VM_USER(vm);
    assert(host && "CS2VM_USER(thread) must be RS_CS2Host*");

    tree = rs_cs2_tree(host);

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_PUSHSCRIPT:
        return exec_push_script(host, vm, request->u.push_script.script_id);

    case CS2VM_HOST_REQUEST_INVS_GET_SIZE:
        return CS2VM2_PushInt(vm, rs_cs2_inv_size(host, request->u.invs_get_size.inv_id));

    case CS2VM_HOST_REQUEST_INVS_GET_OBJ:
        return CS2VM2_PushInt(
            vm,
            rs_cs2_inv_get_obj(
                host, request->u.invs_get_obj.inv_id, request->u.invs_get_obj.slot));

    case CS2VM_HOST_REQUEST_INVS_GET_NUM:
        return CS2VM2_PushInt(
            vm,
            rs_cs2_inv_get_num(
                host, request->u.invs_get_num.inv_id, request->u.invs_get_num.slot));

    case CS2VM_HOST_REQUEST_INVS_GET_TOTAL:
        return CS2VM2_PushInt(
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
        return CS2VM2_PushInt(vm, value);
    }

    case CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING:
    {
        int id = request->u.vars_read_varc_string.varc_id;
        char const* value = "";
        if( id >= 0 && id < RS_CS2_HOST_VARC_STRING_MAX )
            value = host->varc_string[id];
        return CS2VM2_PushStr(vm, strdup(value));
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
        return exec_oc_param(host, vm, request->u.oc_param);

    case CS2VM_HOST_REQUEST_PARAHEIGHT:
        return exec_para_height(host, vm, request->u.para_height, 0);

    case CS2VM_HOST_REQUEST_PARAWIDTH:
        return exec_para_height(host, vm, request->u.para_height, 1);

    case CS2VM_HOST_REQUEST_CLIENTCLOCK:
        return CS2VM2_PushInt(vm, host->client_clock);

    case CS2VM_HOST_REQUEST_IF_SETON_DISCARD:
    case CS2VM_HOST_REQUEST_CC_SETON_DISCARD:
        return CS2VM_EXECNO_OK;

    /* ---- IF getters ---- */
    case CS2VM_HOST_REQUEST_IF_GETWIDTH:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetLayoutWidth(tree, request->u.if_get_width.component_id) : 0);

    case CS2VM_HOST_REQUEST_IF_GETHEIGHT:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetLayoutHeight(tree, request->u.if_get_height.component_id) : 0);

    case CS2VM_HOST_REQUEST_IF_GETX:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetRelativeX(tree, request->u.if_getx.component_id) : 0);

    case CS2VM_HOST_REQUEST_IF_GETY:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetRelativeY(tree, request->u.if_get_width.component_id) : 0);

    case CS2VM_HOST_REQUEST_IF_GETLAYER:
    {
        int parent = tree ? rs_cs2_parent_component_id(tree, request->u.if_get_layer.component_id)
                          : -1;
        return CS2VM2_PushInt(vm, parent >= 0 ? parent : -1);
    }

    case CS2VM_HOST_REQUEST_IF_GETTOP:
        return CS2VM2_PushInt(vm, host->client_type);

    case CS2VM_HOST_REQUEST_IF_GETSCROLLX:
        node = rs_cs2_node(host, request->u.if_get_scroll_x.component_id);
        return CS2VM2_PushInt(vm, node ? node->scroll_x : 0);

    case CS2VM_HOST_REQUEST_IF_GETSCROLLY:
        node = rs_cs2_node(host, request->u.if_get_scroll_y.component_id);
        return CS2VM2_PushInt(vm, node ? node->scroll_y : 0);

    case CS2VM_HOST_REQUEST_IF_GETSCROLLHEIGHT:
        node = rs_cs2_node(host, request->u.if_get_scroll_height.component_id);
        return CS2VM2_PushInt(
            vm, (node && node->type == UIELEM_RS_LAYER) ? node->u.rs_layer.scroll_height : 0);

    case CS2VM_HOST_REQUEST_IF_GETSCROLLWIDTH:
        node = rs_cs2_node(host, request->u.if_getscrollwidth.component_id);
        return CS2VM2_PushInt(
            vm, (node && node->type == UIELEM_RS_LAYER) ? node->u.rs_layer.scroll_width : 0);

    case CS2VM_HOST_REQUEST_IF_GETHIDE:
        node = rs_cs2_node(host, request->u.if_get_width.component_id);
        return CS2VM2_PushInt(vm, node && node->behavior.hide ? 1 : 0);

    case CS2VM_HOST_REQUEST_IF_GETTEXT:
    {
        char buf[512];
        buf[0] = '\0';
        if( tree )
            rs_cs2_get_text(tree, request->u.if_gettext.component_id, buf, (int)sizeof(buf));
        return CS2VM2_PushStr(vm, strdup(buf));
    }

    /* ---- IF / CC mutators ---- */
    case CS2VM_HOST_REQUEST_IF_SETHIDE:
        if( tree )
        {
            int was_hidden = 0;
            int32_t hide_idx;
#if UITREE_CLICK_DEBUG
            fprintf(
                stderr,
                "uitree_click: IF_SETHIDE component_id=%d hide=%d\n",
                request->u.if_set_hide.component_id,
                request->u.if_set_hide.hidden ? 1 : 0);
#endif
            hide_idx = UITree_FindByComponentId(tree, request->u.if_set_hide.component_id);
            if( hide_idx >= 0 )
                was_hidden = tree->components[hide_idx].behavior.hide ? 1 : 0;
            (void)UITree_ApplyHide(
                tree, request->u.if_set_hide.component_id, request->u.if_set_hide.hidden ? 1 : 0);
            /* Unhide → mark widgets-loaded (TS markWidgetsLoaded). Consumed once per
             * logic tick by RS_CS2_PumpTransmits; per-hook serials keep already-fired
             * hooks from re-running, so this no longer re-dispatches everything. */
            if( was_hidden && !request->u.if_set_hide.hidden )
                host->widgets_loaded_dirty = 1;
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETPOSITION:
    case CS2VM_HOST_REQUEST_CC_SETPOSITION:
        if( tree )
            (void)UITree_ApplyPositionModes(
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
        {
#if UITREE_CLICK_DEBUG
            fprintf(
                stderr,
                "uitree_click: SETSIZE component_id=%d size=%dx%d modes=%d,%d\n",
                request->u.cc_set_size.component_id,
                request->u.cc_set_size.width,
                request->u.cc_set_size.height,
                request->u.cc_set_size.wmode,
                request->u.cc_set_size.hmode);
#endif
            (void)UITree_ApplySizeModes(
                tree,
                request->u.cc_set_size.component_id,
                request->u.cc_set_size.width,
                request->u.cc_set_size.height,
                request->u.cc_set_size.wmode,
                request->u.cc_set_size.hmode);
        }
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
            /* Clamp against current computed bounds (reference ensureWidgetLayout
             * before the scroll clamp). */
            UITree_EnsureLayout(tree);
            int max_x = UITree_ScrollMaxX(node);
            int max_y = UITree_ScrollMaxY(node);
            if( sx < 0 )
                sx = 0;
            if( sx > max_x )
                sx = max_x;
            if( sy < 0 )
                sy = 0;
            if( sy > max_y )
                sy = max_y;
            (void)UITree_ApplyScrollPos(tree, cid, sx, sy);
        }
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_IF_SETSCROLLSIZE:
    case CS2VM_HOST_REQUEST_CC_SETSCROLLSIZE:
        if( tree &&
            UITree_ApplyScrollSize(
                tree,
                request->u.if_set_scroll_size.component_id,
                request->u.if_set_scroll_size.scroll_width,
                request->u.if_set_scroll_size.scroll_height) )
        {
            /* Reference revalidateWidgetScroll: re-clamp scroll offsets after
             * the scroll area changes. */
            node = rs_cs2_node(host, request->u.if_set_scroll_size.component_id);
            if( node && node->type == UIELEM_RS_LAYER )
            {
                UITree_EnsureLayout(tree);
                UITree_ScrollClampComponent(node);
            }
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETGRAPHIC:
    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC:
        return exec_set_graphic(host, vm, request->u.cc_set_graphic);

    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC2:
        node = rs_cs2_node(host, request->u.cc_set_graphic2.component_id);
        if( node && node->type == UIELEM_RS_GRAPHIC )
        {
            node->u.rs_graphic.scene_id_active = request->u.cc_set_graphic2.graphic_id;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_graphic2.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETTEXT:
    case CS2VM_HOST_REQUEST_CC_SETTEXT:
#if UITREE_CLICK_DEBUG
        fprintf(
            stderr,
            "uitree_click: SETTEXT component_id=%d text=\"%.48s\"\n",
            request->u.cc_set_text.component_id,
            request->u.cc_set_text.text ? request->u.cc_set_text.text : "");
#endif
        if( tree )
            (void)UITree_ApplyText(
                tree, request->u.cc_set_text.component_id, request->u.cc_set_text.text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETOUTLINE:
        if( tree )
            (void)UITree_ApplyGraphicOutline(
                tree, request->u.if_set_outline.component_id, request->u.if_set_outline.outline);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETOUTLINE:
        if( tree )
            (void)UITree_ApplyGraphicOutline(
                tree, request->u.cc_set_outline.component_id, request->u.cc_set_outline.outline);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTILING:
        if( tree )
            (void)UITree_ApplyGraphicTiled(
                tree, request->u.cc_set_tiling.component_id, request->u.cc_set_tiling.tiling);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETGRAPHICSHADOW:
        if( tree )
            (void)UITree_ApplyGraphicShadow(
                tree,
                request->u.cc_set_graphic_shadow.component_id,
                request->u.cc_set_graphic_shadow.shadow);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETCOLOUR:
        if( tree )
            (void)UITree_ApplyColour(
                tree, request->u.cc_set_colour.component_id, request->u.cc_set_colour.colour);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETFILL:
        node = rs_cs2_node(host, request->u.cc_set_fill.component_id);
        if( node && node->type == UIELEM_RS_RECT )
        {
            node->u.rs_rect.filled = request->u.cc_set_fill.filled ? 1 : 0;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_fill.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTRANS:
        node = rs_cs2_node(host, request->u.cc_set_trans.component_id);
        if( node )
        {
            node->trans = request->u.cc_set_trans.trans;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_trans.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETNOCLICKTHROUGH:
        node = rs_cs2_node(host, request->u.cc_set_no_click_through.component_id);
        if( node )
        {
            node->no_click_through = request->u.cc_set_no_click_through.enabled ? 1 : 0;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_no_click_through.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTEXTFONT:
        return exec_set_text_font(host, vm, request->u.cc_set_text_font);

    case CS2VM_HOST_REQUEST_CC_SETTEXTALIGN:
        if( tree )
            (void)UITree_ApplyTextAlign(
                tree,
                request->u.cc_set_text_align.component_id,
                request->u.cc_set_text_align.x_align,
                request->u.cc_set_text_align.y_align,
                request->u.cc_set_text_align.line_height);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW:
        if( tree )
            (void)UITree_ApplyTextShadow(
                tree,
                request->u.cc_set_text_shadow.component_id,
                request->u.cc_set_text_shadow.shadowed);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLE:
    case CS2VM_HOST_REQUEST_IF_SETDRAGGABLE:
        node = rs_cs2_node(host, request->u.cc_set_draggable.component_id);
        if( node )
        {
            node->draggable = 1;
            node->drag_render_area_uid = request->u.cc_set_draggable.parent_uid;
            node->drag_render_area_child_index = request->u.cc_set_draggable.child_index;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_draggable.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLEBEHAVIOR:
    case CS2VM_HOST_REQUEST_IF_SETDRAGGABLEBEHAVIOR:
        node = rs_cs2_node(host, request->u.cc_set_draggable_behavior.component_id);
        if( node )
        {
            node->drag_behavior = request->u.cc_set_draggable_behavior.behavior;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_draggable_behavior.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADZONE:
        node = rs_cs2_node(host, request->u.cc_set_drag_dead_zone.component_id);
        if( node )
        {
            node->drag_dead_zone = (uint8_t)request->u.cc_set_drag_dead_zone.zone;
            UITree_MarkNodeDirty(
                tree, rs_cs2_find_node(host, request->u.cc_set_drag_dead_zone.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADTIME:
        node = rs_cs2_node(host, request->u.cc_set_drag_dead_time.component_id);
        if( node )
        {
            node->drag_dead_time = (uint8_t)request->u.cc_set_drag_dead_time.time;
            UITree_MarkNodeDirty(
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
            tree ? UITree_FindByComponentId(tree, request->u.cc_delete_all.component_id) : -1;
        if( parent_idx >= 0 )
            UITree_CcDeleteAll(tree, parent_idx);
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_CC_CREATE:
        return exec_cc_create(host, vm, request->u.cc_create);
    case CS2VM_HOST_REQUEST_CC_COPY:
        return exec_cc_copy(host, vm, request->u.cc_copy);

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
        return CS2VM2_PushInt(vm, found);
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
        return CS2VM2_PushInt(vm, parent);
    }

    case CS2VM_HOST_REQUEST_CC_GETID:
        node = rs_cs2_node(host, request->u.cc_get_id.component_id);
        assert(node);

        return CS2VM2_PushInt(vm, node->dynamic ? node->dynamic_child_index : -1);

    case CS2VM_HOST_REQUEST_CC_GETX:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetRelativeX(tree, request->u.cc_get_id.component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETY:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetRelativeY(tree, request->u.cc_get_id.component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETWIDTH:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetLayoutWidth(tree, request->u.cc_get_id.component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETHEIGHT:
        return CS2VM2_PushInt(
            vm, tree ? UITree_GetLayoutHeight(tree, request->u.cc_get_id.component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETHIDE:
        node = rs_cs2_node(host, request->u.cc_get_id.component_id);
        return CS2VM2_PushInt(vm, node && node->behavior.hide ? 1 : 0);

    case CS2VM_HOST_REQUEST_CC_GETTEXT:
    {
        char buf[512];
        buf[0] = '\0';
        if( tree )
            rs_cs2_get_text(tree, request->u.cc_gettext.component_id, buf, (int)sizeof(buf));
        return CS2VM2_PushStr(vm, strdup(buf));
    }

    case CS2VM_HOST_REQUEST_CC_GETTRANS:
        node = rs_cs2_node(host, request->u.cc_gettrans.component_id);
        return CS2VM2_PushInt(vm, node ? node->trans : 0);

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
            (void)UITree_ApplyOpBase(
                tree, request->u.if_set_op_base.component_id, request->u.if_set_op_base.text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETOPSUBMENU:
        if( tree )
            rs_cs2_apply_op_submenu(
                tree,
                request->u.if_set_op_submenu.component_id,
                request->u.if_set_op_submenu.op_index,
                request->u.if_set_op_submenu.sub_index,
                request->u.if_set_op_submenu.text);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETTARGETPRIORITY:
        if( tree )
            (void)UITree_ApplyTargetPriority(
                tree,
                request->u.if_set_target_priority.component_id,
                request->u.if_set_target_priority.priority);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_CLEAROPS:
        if( tree )
            rs_cs2_clear_ops(tree, request->u.if_clear_ops.component_id);
        return CS2VM_EXECNO_OK;

    /* ---- SetOn (hooks / no-ops) ---- */
    case CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT:
        return exec_set_on_var_transmit(host, &request->u.if_set_on_var_transmit);

    case CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT:
        return exec_set_on_inv_transmit(host, &request->u.if_set_on_inv_transmit);

    case CS2VM_HOST_REQUEST_IF_SETONOP:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONCLICK:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONHOLD:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONDRAG:
    case CS2VM_HOST_REQUEST_IF_SETONDRAGCOMPLETE:
    case CS2VM_HOST_REQUEST_IF_SETONRESIZE:
    case CS2VM_HOST_REQUEST_IF_SETONSUBCHANGE:
    case CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT:
    case CS2VM_HOST_REQUEST_IF_SETONTIMER:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONKEY:
    case CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT:
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETONCLICK:
    case CS2VM_HOST_REQUEST_CC_SETONHOLD:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE:
    case CS2VM_HOST_REQUEST_CC_SETONOP:
    case CS2VM_HOST_REQUEST_CC_SETONDRAG:
    case CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE:
    case CS2VM_HOST_REQUEST_CC_SETONRESIZE:
    case CS2VM_HOST_REQUEST_CC_SETONSUBCHANGE:
    case CS2VM_HOST_REQUEST_CC_SETONSCROLLWHEEL:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEREPEAT:
    case CS2VM_HOST_REQUEST_CC_SETONTIMER:
        return exec_set_on_cc_event(host, vm, request->kind, &request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_CC_SETONVARTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONINVTRANSMIT:
        return exec_set_on_cc_transmit(host, vm, request->kind, &request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_CC_SETONKEY:
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_SETANTIDRAG:
        if( tree )
            tree->anti_drag = request->u.widget_set_int.value ? 1 : 0;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_IF_DRAGPICKUP:
    case CS2VM_HOST_REQUEST_CC_DRAGPICKUP:
        /* Demo: mark component drag_active; full pickup offsets applied by input loop. */
        node = rs_cs2_node(host, request->u.widget_set_int.component_id);
        if( node )
        {
            node->draggable = 1;
            node->drag_active = 1;
            UITree_MarkNodeDirty(tree, rs_cs2_find_node(host, request->u.widget_set_int.component_id));
        }
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
            "RS_CS2Host_Exec: UNHANDLED request kind %d\n",
            (int)request->kind);
        return CS2VM_EXECNO_ERROR;
    }
}
