#include "game/rs_cs2_host.h"

#include "cs2vm2/cs2vm2.h"
#include "engine/cache_provider.h"
#include "engine/task_obj_model_load.h"
#include "engine/torirs_types.h"
#include "engine/uitree_scene_bridge.h"
#include "engine/torirs_worldmap_from_rscache.h"
#include "game/rs_worldmap.h"
#include "inv/inv_manager.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"
#include "ui/uitree_scroll.h"
#include "varc/varc_manager.h"
#include "varp/varp_manager.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UITREE_CLICK_DEBUG
#define UITREE_CLICK_DEBUG 0
#endif

/** UIZOOM_RESET / UIZOOM_GETDEFAULT constant (1000 = 100%, reference scheme). */
#define RS_CS2_UIZOOM_DEFAULT 1000

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

/**
 * One opcode, one yield. `rs_cs2_yield_load` parks a request for the task layer
 * and records what it is waiting for; `rs_cs2_await_spent` tells the handler on
 * the retry that this exact wait already happened, so a resource that is still
 * missing is genuinely absent and the handler must complete with a default
 * rather than yield again (which the VM's yield-halt guard treats as a bug).
 *
 * `id2` carries the second resource of a two-resource request (obj + param
 * type, struct + param type); pass -1 when there is only one.
 */
static bool
rs_cs2_await_spent(
    struct RS_CS2Host* host,
    enum CS2VM_HostRequestKind kind,
    int id,
    int id2)
{
    assert(host);
    return host->has_awaited && host->awaited_kind == kind && host->awaited_id == id &&
           host->awaited_id2 == id2;
}

static int
rs_cs2_yield_load(
    struct RS_CS2Host* host,
    struct CS2VM_HostRequest const* request,
    int id,
    int id2)
{
    assert(host);
    assert(request);
    host->awaited_kind = request->kind;
    host->awaited_id = id;
    host->awaited_id2 = id2;
    host->has_awaited = true;
    return rs_cs2_yield(host, request);
}

/**
 * Yield when the target's interface group is not mounted yet. The test is the
 * group *root* (`group<<16`), never the exact component: the task layer loads
 * and bakes a whole pack, so once the root is present a still-missing child
 * cannot be conjured by loading again — the caller treats it as not-found.
 */
static int
rs_cs2_yield_if_group_missing(
    struct RS_CS2Host* host,
    int component_id,
    struct CS2VM_HostRequest const* request)
{
    int group_id;

    assert(host);
    assert(request);

    group_id = (component_id >> 16) & 0xffff;
    if( component_id <= 0 || group_id <= 0 )
        return CS2VM_EXECNO_OK;

    if( rs_cs2_find_node(host, group_id << 16) >= 0 )
        return CS2VM_EXECNO_OK;

    if( rs_cs2_await_spent(host, request->kind, group_id, -1) )
        return CS2VM_EXECNO_OK;

    if( getenv("TORIRS_CS2_MOUNT_DEBUG") )
        fprintf(
            stderr,
            "cs2-automount: group %d requested via component 0x%08x (req kind=%d)\n",
            group_id,
            (unsigned)component_id,
            (int)request->kind);
    return rs_cs2_yield_load(host, request, group_id, -1);
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
    int j;
    assert(tree);
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return;
    for( i = 0; i < UITREE_MENU_OPTION_SLOTS; i++ )
        tree->components[idx].menu_options.ops[i][0] = '\0';
    tree->components[idx].menu_options.option[0] = '\0';
    for( i = 0; i < UITREE_SUBMENU_OP_SLOTS; i++ )
        for( j = 0; j < UITREE_SUBMENU_ENTRY_SLOTS; j++ )
            tree->components[idx].menu_options.submenus.ops[i][j][0] = '\0';
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
    /* Script indices are 1-based (same convention as rs_cs2_apply_op). */
    if( op_index < 1 || op_index > UITREE_SUBMENU_OP_SLOTS )
        return;
    if( sub_index < 1 || sub_index > UITREE_SUBMENU_ENTRY_SLOTS )
        return;
    idx = UITree_FindByComponentId(tree, component_id);
    if( idx < 0 )
        return;
    strncpy(
        tree->components[idx].menu_options.submenus.ops[op_index - 1][sub_index - 1],
        text ? text : "",
        UITREE_MENU_OPTION_LEN - 1);
    tree->components[idx]
        .menu_options.submenus.ops[op_index - 1][sub_index - 1][UITREE_MENU_OPTION_LEN - 1] = '\0';
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
    struct VarPManager* varps,
    struct VarCManager* varcs)
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
    host->varcs = varcs;
    host->client_clock = 100;
    host->client_type = 80;
    host->cam_follow_height = 0;
    /* Volumes start muted (0); a settings panel or the server drives them up.
     * memset already zeroed these — kept explicit alongside the getters they back. */
    host->volume_music = 0;
    host->volume_sounds = 0;
    host->volume_area_sounds = 0;
    /* Start at the low end of the documented 2..8 range; a settings panel or the
     * server can drive it. Real default is TBD once minimap zoom is rendered. */
    host->minimap_zoom = 2;
    host->logout_requested = false;
    /* Preserve what VIEWPORT_GETFOV/GETZOOM returned before they were
     * host-routed (cs2_host_ui.c defaults for fixed-layout clients). */
    host->viewport_fov = 128;
    host->viewport_fov_max = 896;
    host->viewport_zoom = 128;
    host->viewport_zoom_max = 896;
    host->ui_zoom = RS_CS2_UIZOOM_DEFAULT;
    /* Facing north; nothing writes this yet (no CAM_SETYAW, no live camera link). */
    host->cam_yaw = 0;
    /* Op 1 is the primary left-click op, which is what every mouse-driven
     * dispatch reports. Must not be left at the memset 0: on_op handlers read
     * this through the CS2VM_SCRIPT_ARG_OP_INDEX sentinel. */
    host->event_op_index = 1;
    /* No key event in flight: a key CODE of -1 means "this is a character
     * event", so 0 (a real code) would be a lie. */
    host->event_key_typed = -1;
    host->viewport_w = 765;
    host->viewport_h = 503;
    host->bridge = NULL;
    host->has_awaited = false;
    /* Serials start at 1 so fresh hooks (last_seen_serial=0) fire once on the
     * first dispatch after registration (widget-loaded parity). */
    host->var_change_serial = 1;
    host->inv_change_serial = 1;
    host->worldmap = RS_WorldMap_New(provider);
}

void
RS_CS2Host_NotifyVarChanged(
    struct RS_CS2Host* host,
    int var_id)
{
    (void)var_id; /* re-dispatch re-checks all hooks, gated by serial + hidden */
    if( !host )
        return;
    /* Advance the serial so already-fired var-transmit hooks re-run, and flag the
     * per-tick pump to re-dispatch (TS parity: value changes bump changedVarpCount,
     * processed once per cycle rather than synchronously mid-script). */
    host->var_change_serial++;
    host->var_transmit_dirty = 1;
}

void
RS_CS2Host_Free(struct RS_CS2Host* host)
{
    assert(host);
    RS_WorldMap_Free(host->worldmap);
    host->worldmap = NULL;
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
        if( rs_cs2_await_spent(host, req.kind, script_id, -1) )
        {
            /* No degrade possible: the caller expects this script's return
             * values on the stack and we cannot synthesise them. */
            fprintf(stderr, "RS_CS2Host: script %d failed to load\n", script_id);
            return CS2VM_EXECNO_ERROR;
        }
        return rs_cs2_yield_load(host, &req, script_id, -1);
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
            /* Font still missing after its load: measure as 0. */
            if( !rs_cs2_await_spent(host, req.kind, request.font_id, -1) )
                return rs_cs2_yield_load(host, &req, request.font_id, -1);
        }
        else
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
        if( !rs_cs2_await_spent(host, req.kind, request.enum_id, -1) )
            return rs_cs2_yield_load(host, &req, request.enum_id, -1);
        /* Enum still missing after its load: answer like a key that misses. */
        if( request.output_type == (int)'s' )
            return CS2VM2_PushStr(thread, strdup("null"));
        return CS2VM2_PushInt(thread, -1);
    }

    (void)request.input_type;
    if( request.output_type == (int)'s' || e->output_is_string )
    {
        char const* value = rs_cs2_enum_lookup_string(e, request.key);
        return CS2VM2_PushStr(thread, strdup(value ? value : "null"));
    }

    return CS2VM2_PushInt(thread, rs_cs2_enum_lookup_int(e, request.key));
}

/* =========================================================================
 * World map (interface 595)
 * ========================================================================= */

/* Pushing two ints for a getter that returns a pair: the script pops them in
 * reverse, so push order is (first, second). */
static int
rs_cs2_push_pair(
    struct CS2VM2_Thread* thread,
    int first,
    int second)
{
    int result = CS2VM2_PushInt(thread, first);
    if( result != CS2VM_EXECNO_OK )
        return result;
    return CS2VM2_PushInt(thread, second);
}

static int
exec_worldmap(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_WorldMap request)
{
    struct RS_WorldMapState* map = host->worldmap;
    struct ToriRS_WorldMapArea* area;
    int first = -1;
    int second = -1;

    if( !map )
        return CS2VM_EXECNO_ERROR;

    /* The areas load once for the whole cache. Yield for that load the first
     * time any world map opcode runs; if they are still missing on the retry,
     * this cache has no world map and every getter answers "nothing". */
    if( !RS_WorldMap_Sync(map) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_WORLDMAP;
        req.u.worldmap = request;
        if( !rs_cs2_await_spent(host, req.kind, -1, -1) )
            return rs_cs2_yield_load(host, &req, -1, -1);
    }

    switch( request.opcode )
    {
    case CS2_OP_WORLDMAP_INIT:
        RS_WorldMap_Init(map);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_GETMAPNAME:
        area = RS_WorldMap_Area(map, request.arg0);
        return CS2VM2_PushStr(
            thread, strdup(area && area->external_name ? area->external_name : ""));

    case CS2_OP_WORLDMAP_SETMAP:
        RS_WorldMap_SetCurrentMapId(map, request.arg0);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_GETZOOM:
        return CS2VM2_PushInt(thread, RS_WorldMap_Zoom(map));

    case CS2_OP_WORLDMAP_SETZOOM:
        RS_WorldMap_SetZoom(map, request.arg0);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_ISLOADED:
        return CS2VM2_PushInt(thread, RS_WorldMap_IsLoaded(map) ? 1 : 0);

    case CS2_OP_WORLDMAP_JUMPTODISPLAYCOORD:
        RS_WorldMap_JumpToDisplayCoord(map, request.arg0, false);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_JUMPTODISPLAYCOORD_INSTANT:
        RS_WorldMap_JumpToDisplayCoord(map, request.arg0, true);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_JUMPTOSOURCECOORD:
        RS_WorldMap_JumpToSourceCoord(map, request.arg0, false);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_JUMPTOSOURCECOORD_INSTANT:
        RS_WorldMap_JumpToSourceCoord(map, request.arg0, true);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_GETDISPLAYPOSITION:
        RS_WorldMap_DisplayPosition(map, &first, &second);
        return rs_cs2_push_pair(thread, first, second);

    case CS2_OP_WORLDMAP_GETCONFIGORIGIN:
        area = RS_WorldMap_Area(map, request.arg0);
        return CS2VM2_PushInt(thread, area ? area->origin : 0);

    case CS2_OP_WORLDMAP_GETCONFIGSIZE:
        area = RS_WorldMap_Area(map, request.arg0);
        return rs_cs2_push_pair(
            thread,
            ToriRS_WorldMapArea_WidthTiles(area),
            ToriRS_WorldMapArea_HeightTiles(area));

    case CS2_OP_WORLDMAP_GETCONFIGBOUNDS:
    {
        int min_x = 0;
        int min_y = 0;
        int max_x = 0;
        int max_y = 0;
        int result;

        area = RS_WorldMap_Area(map, request.arg0);
        ToriRS_WorldMapArea_Bounds(area, &min_x, &min_y, &max_x, &max_y);
        result = rs_cs2_push_pair(thread, min_x, min_y);
        if( result != CS2VM_EXECNO_OK )
            return result;
        return rs_cs2_push_pair(thread, max_x, max_y);
    }

    case CS2_OP_WORLDMAP_GETCONFIGZOOM:
        area = RS_WorldMap_Area(map, request.arg0);
        return CS2VM2_PushInt(thread, area ? area->zoom : -1);

    case CS2_OP_WORLDMAP_GETDISPLAYCOORD_CURRENT:
        if( !RS_WorldMap_DisplayCoord(map, &first, &second) )
        {
            first = -1;
            second = -1;
        }
        return rs_cs2_push_pair(thread, first, second);

    case CS2_OP_WORLDMAP_GETCURRENTMAP:
        return CS2VM2_PushInt(thread, RS_WorldMap_CurrentMapId(map));

    case CS2_OP_WORLDMAP_GETDISPLAYCOORD:
        if( !RS_WorldMap_SourceToDisplay(map, request.arg0, &first, &second) )
        {
            first = -1;
            second = -1;
        }
        return rs_cs2_push_pair(thread, first, second);

    case CS2_OP_WORLDMAP_GETSOURCECOORD:
        return CS2VM2_PushInt(thread, RS_WorldMap_DisplayToSource(map, request.arg0));

    case CS2_OP_WORLDMAP_JUMPTOMAP:
        RS_WorldMap_JumpToMap(map, request.arg0, request.arg1, false);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_JUMPTOMAP_INSTANT:
        RS_WorldMap_JumpToMap(map, request.arg0, request.arg1, true);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_COORDINMAP:
        return CS2VM2_PushInt(
            thread, RS_WorldMap_CoordInMap(map, request.arg0, request.arg1) ? 1 : 0);

    case CS2_OP_WORLDMAP_GETSIZE:
        RS_WorldMap_DisplaySize(map, &first, &second);
        return rs_cs2_push_pair(thread, first, second);

    case CS2_OP_WORLDMAP_GETMAP:
        return CS2VM2_PushInt(thread, RS_WorldMap_MapAtCoord(map, request.arg0));

    case CS2_OP_WORLDMAP_SETMAXFLASHCOUNT:
        RS_WorldMap_SetMaxFlashCount(map, request.arg0);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_RESETMAXFLASHCOUNT:
        RS_WorldMap_ResetMaxFlashCount(map);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_SETCYCLESPERFLASH:
        RS_WorldMap_SetCyclesPerFlash(map, request.arg0);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_RESETCYCLESPERFLASH:
        RS_WorldMap_ResetCyclesPerFlash(map);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_GETNEARESTICON:
        return CS2VM2_PushInt(
            thread, RS_WorldMap_NearestIcon(map, request.arg0, request.arg1));

    case CS2_OP_WORLDMAP_PERPETUALFLASH:
        RS_WorldMap_SetPerpetualFlash(map, request.arg0 == 1);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_FLASHELEMENT:
        RS_WorldMap_FlashElement(map, request.arg0);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_FLASHELEMENTCATEGORY:
        RS_WorldMap_FlashCategory(map, request.arg0);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_STOPCURRENTFLASHES:
        RS_WorldMap_StopCurrentFlashes(map);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_DISABLEELEMENTS:
        RS_WorldMap_SetElementsEnabled(map, request.arg0 == 1);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_DISABLEELEMENT:
        RS_WorldMap_SetElementEnabled(map, request.arg0, request.arg1 == 1);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_DISABLEELEMENTCATEGORY:
        RS_WorldMap_SetCategoryEnabled(map, request.arg0, request.arg1 == 1);
        return CS2VM_EXECNO_OK;

    case CS2_OP_WORLDMAP_GETDISABLEELEMENTS:
        return CS2VM2_PushInt(thread, RS_WorldMap_ElementsEnabled(map) ? 1 : 0);

    case CS2_OP_WORLDMAP_GETDISABLEELEMENT:
        return CS2VM2_PushInt(thread, RS_WorldMap_IsElementEnabled(map, request.arg0) ? 1 : 0);

    case CS2_OP_WORLDMAP_GETDISABLEELEMENTCATEGORY:
        return CS2VM2_PushInt(thread, RS_WorldMap_IsCategoryEnabled(map, request.arg0) ? 1 : 0);

    case CS2_OP_WORLDMAP_LISTELEMENT_START:
        if( !RS_WorldMap_IconStart(map, &first, &second) )
        {
            first = -1;
            second = -1;
        }
        return rs_cs2_push_pair(thread, first, second);

    case CS2_OP_WORLDMAP_LISTELEMENT_NEXT:
        if( !RS_WorldMap_IconNext(map, &first, &second) )
        {
            first = -1;
            second = -1;
        }
        return rs_cs2_push_pair(thread, first, second);

    case CS2_OP_WORLDMAP_ELEMENT:
        return CS2VM2_PushInt(thread, map->event_element);

    case CS2_OP_WORLDMAP_ELEMENTCOORD1:
        return CS2VM2_PushInt(thread, map->event_coord1);

    case CS2_OP_WORLDMAP_ELEMENTCOORD:
        return CS2VM2_PushInt(thread, map->event_coord2);

    default:
        fprintf(stderr, "exec_worldmap: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

static int
exec_mec(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_MEC request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_MapElement* element =
        provider ? CacheProvider_MapElementGet(provider, request.mec_id) : NULL;

    if( !element )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_MEC;
        req.u.mec = request;
        if( !rs_cs2_await_spent(host, req.kind, request.mec_id, -1) )
            return rs_cs2_yield_load(host, &req, request.mec_id, -1);
        /* Still missing after its load: answer as the reference does for an
         * absent map element config. */
        if( request.opcode == CS2_OP_MEC_TEXT )
            return CS2VM2_PushStr(thread, strdup(""));
        if( request.opcode == CS2_OP_MEC_TEXTSIZE )
            return CS2VM2_PushInt(thread, 0);
        return CS2VM2_PushInt(thread, -1);
    }

    switch( request.opcode )
    {
    case CS2_OP_MEC_TEXT:
        return CS2VM2_PushStr(thread, strdup(element->name ? element->name : ""));
    case CS2_OP_MEC_TEXTSIZE:
        return CS2VM2_PushInt(thread, element->text_size);
    case CS2_OP_MEC_CATEGORY:
        return CS2VM2_PushInt(thread, element->category);
    case CS2_OP_MEC_SPRITE:
        return CS2VM2_PushInt(thread, element->sprite_id);
    default:
        fprintf(stderr, "exec_mec: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/*
 * MINIMENU_* (7100..7110): mouseover / right-click-menu queries. The live model
 * lives in the app layer (app->interact.minimenu, plus the hover-text target)
 * behind the UITree host bus, which the CS2 host cannot reach. Until a per-frame
 * snapshot is plumbed into RS_CS2Host, answer with "nothing hovered / menu
 * closed" defaults so the polling toplevel scripts keep running: every int getter
 * is 0/false and MINIMENU_ENTRY yields two empty strings. Wire real values here
 * when the snapshot lands — the opcode already routes through this one seam.
 */
static int
exec_minimenu(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    int opcode)
{
    (void)host;

    switch( opcode )
    {
    case CS2_OP_MINIMENU_ENTRY:
    {
        /* Two strings, option then target (reference push order). */
        int result = CS2VM2_PushStr(thread, strdup(""));
        if( result != CS2VM_EXECNO_OK )
            return result;
        return CS2VM2_PushStr(thread, strdup(""));
    }
    case CS2_OP_MINIMENU_TYPE:
    case CS2_OP_MINIMENU_FINDNPC:
    case CS2_OP_MINIMENU_FINDLOC:
    case CS2_OP_MINIMENU_FINDOBJ:
    case CS2_OP_MINIMENU_FINDPLAYER:
    case CS2_OP_MINIMENU_ISOPEN:
    case CS2_OP_MINIMENU_FINDCOMPONENT:
    case CS2_OP_MINIMENU_NUMOPS:
        return CS2VM2_PushInt(thread, 0);
    default:
        fprintf(stderr, "exec_minimenu: unhandled opcode %d\n", opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/*
 * Audio volumes (3203..3208) and client/game/device options (3209..3217). The
 * volume values are host-owned (round-trip: a SET is read back by the matching
 * GET), the port having no audio mixer yet. The keyed option families have no
 * backing store here — SET is a no-op, GET answers 0, GETRANGE answers a 0..255
 * span — enough for a settings panel to run; wire real option state through here
 * when it exists (request.option_id / request.value carry the key + payload).
 */
static int
exec_client_option(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_ClientOption request)
{
    switch( request.opcode )
    {
    case CS2_OP_SETVOLUMEMUSIC:
        host->volume_music = request.value;
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETVOLUMEMUSIC:
        return CS2VM2_PushInt(thread, host->volume_music);
    case CS2_OP_SETVOLUMESOUNDS:
        host->volume_sounds = request.value;
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETVOLUMESOUNDS:
        return CS2VM2_PushInt(thread, host->volume_sounds);
    case CS2_OP_SETVOLUMEAREASOUNDS:
        host->volume_area_sounds = request.value;
        return CS2VM_EXECNO_OK;
    case CS2_OP_GETVOLUMEAREASOUNDS:
        return CS2VM2_PushInt(thread, host->volume_area_sounds);

    case CS2_OP_CLIENTOPTION_SET:
    case CS2_OP_GAMEOPTION_SET:
    case CS2_OP_DEVICEOPTION_SET:
        return CS2VM_EXECNO_OK;
    case CS2_OP_CLIENTOPTION_GET:
    case CS2_OP_GAMEOPTION_GET:
    case CS2_OP_DEVICEOPTION_GET:
        return CS2VM2_PushInt(thread, 0);
    case CS2_OP_DEVICEOPTION_GETRANGE:
    {
        /* min then max (reference range order). */
        int result = CS2VM2_PushInt(thread, 0);
        if( result != CS2VM_EXECNO_OK )
            return result;
        return CS2VM2_PushInt(thread, 255);
    }
    default:
        fprintf(stderr, "exec_client_option: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/*
 * Minimap zoom controls (7250..7254). The zoom is host-owned (round-trip: a
 * SETZOOM is read back by GETZOOM), the port having no minimap-zoom render path
 * yet. SETZOOMABLE and SETICONZOOMLIMIT have no backing state — accepted and
 * dropped (request.value is there for when they gain a render effect).
 */
static int
exec_minimap(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_Minimap request)
{
    switch( request.opcode )
    {
    case CS2_OP_MINIMAP_SETZOOM:
        host->minimap_zoom = request.value;
        return CS2VM_EXECNO_OK;
    case CS2_OP_MINIMAP_GETZOOM:
        return CS2VM2_PushInt(thread, host->minimap_zoom);
    case CS2_OP_MINIMAP_SETZOOMABLE:
    case CS2_OP_MINIMAP_SETICONZOOMLIMIT:
        return CS2VM_EXECNO_OK;
    default:
        fprintf(stderr, "exec_minimap: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/*
 * Viewport FOV/zoom (6200..6205). The host owns both value/max pairs — a
 * SETFOV/SETZOOM (or CLAMPFOV) round-trips through the matching GET, unlike
 * before this opcode was host-routed, when GETFOV/GETZOOM answered a hardcoded
 * constant no SET could ever change. CLAMPFOV's exact arg order is inferred
 * (value, min, max, unused) from its established (4,0,0,0) stack signature —
 * there's no reference decompile to confirm it against.
 */
static int
exec_viewport(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_Viewport request)
{
    switch( request.opcode )
    {
    case CS2_OP_VIEWPORT_SETFOV:
        host->viewport_fov = request.args[0];
        host->viewport_fov_max = request.args[1];
        return CS2VM_EXECNO_OK;
    case CS2_OP_VIEWPORT_GETFOV:
    {
        int result = CS2VM2_PushInt(thread, host->viewport_fov);
        if( result != CS2VM_EXECNO_OK )
            return result;
        return CS2VM2_PushInt(thread, host->viewport_fov_max);
    }
    case CS2_OP_VIEWPORT_SETZOOM:
        host->viewport_zoom = request.args[0];
        host->viewport_zoom_max = request.args[1];
        return CS2VM_EXECNO_OK;
    case CS2_OP_VIEWPORT_GETZOOM:
    {
        int result = CS2VM2_PushInt(thread, host->viewport_zoom);
        if( result != CS2VM_EXECNO_OK )
            return result;
        return CS2VM2_PushInt(thread, host->viewport_zoom_max);
    }
    case CS2_OP_VIEWPORT_CLAMPFOV:
    {
        int value = request.args[0];
        int min = request.args[1];
        int max = request.args[2];
        if( value < min )
            value = min;
        if( value > max )
            value = max;
        host->viewport_fov = value;
        host->viewport_fov_max = max;
        return CS2VM_EXECNO_OK;
    }
    default:
        fprintf(stderr, "exec_viewport: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/* UI zoom (6210..6214). SET/GET/RESET are host-owned state; GETDEFAULT answers
 * the fixed RS_CS2_UIZOOM_DEFAULT constant without touching that state. */
static int
exec_uizoom(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_UiZoom request)
{
    switch( request.opcode )
    {
    case CS2_OP_UIZOOM_SET:
        host->ui_zoom = request.value;
        return CS2VM_EXECNO_OK;
    case CS2_OP_UIZOOM_GET:
        return CS2VM2_PushInt(thread, host->ui_zoom);
    case CS2_OP_UIZOOM_RESET:
        host->ui_zoom = RS_CS2_UIZOOM_DEFAULT;
        return CS2VM_EXECNO_OK;
    case CS2_OP_UIZOOM_GETDEFAULT:
        return CS2VM2_PushInt(thread, RS_CS2_UIZOOM_DEFAULT);
    default:
        fprintf(stderr, "exec_uizoom: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
}

/* Safe-area bounds (6220..6223, 6231). Desktop client, no notch/home indicator:
 * min corners are 0, max corners are the live canvas size (same source as
 * GETCANVASSIZE / VIEWPORT_GETEFFECTIVESIZE). GETMAXY_ALT (6231) is the same
 * value as GETMAXY per the "alternative opcode used in some contexts" note. */
static int
exec_safearea(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_SafeArea request)
{
    switch( request.opcode )
    {
    case CS2_OP_SAFEAREA_GETMINX:
    case CS2_OP_SAFEAREA_GETMINY:
        return CS2VM2_PushInt(thread, 0);
    case CS2_OP_SAFEAREA_GETMAXX:
        return CS2VM2_PushInt(thread, thread->canvas_w);
    case CS2_OP_SAFEAREA_GETMAXY:
    case CS2_OP_SAFEAREA_GETMAXY_ALT:
        return CS2VM2_PushInt(thread, thread->canvas_h);
    default:
        fprintf(stderr, "exec_safearea: unhandled opcode %d\n", request.opcode);
        return CS2VM_EXECNO_ERROR;
    }
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
        if( !rs_cs2_await_spent(host, req.kind, request.enum_id, -1) )
            return rs_cs2_yield_load(host, &req, request.enum_id, -1);
        /* Enum still missing after its load: an empty enum has no outputs. */
        return CS2VM2_PushInt(thread, 0);
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
    struct ToriRS_ParamType* param =
        provider ? CacheProvider_ParamGet(provider, request.param_id) : NULL;

    /* Both configs are needed: the struct carries the value, the ParamType
     * decides string-vs-int and supplies the default the struct may omit. One
     * yield loads both. struct -1 ("no struct") is a valid script input — an
     * enum lookup that misses pushes -1 straight into struct_param — so it is
     * never awaited, it just falls through to the param default. */
    if( (!s && request.struct_id >= 0) || (!param && request.param_id >= 0) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_STRUCT_PARAM;
        req.u.struct_param = request;
        if( !rs_cs2_await_spent(host, req.kind, request.struct_id, request.param_id) )
            return rs_cs2_yield_load(host, &req, request.struct_id, request.param_id);
        /* Still missing after the load: complete with whatever did arrive. */
    }

    found = s && rs_cs2_struct_param_lookup(s, request.param_id, &is_string, &intval, &strval);
    if( param && param->is_string )
    {
        if( found && strval )
            return CS2VM2_PushStr(thread, strdup(strval));
        return CS2VM2_PushStr(
            thread, strdup(param->default_string ? param->default_string : ""));
    }
    if( found && is_string )
        return CS2VM2_PushStr(thread, strdup(strval ? strval : ""));
    if( found )
        return CS2VM2_PushInt(thread, intval);
    return CS2VM2_PushInt(thread, param ? param->default_int : 0);
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

    /* Objtype and ParamType both feed the answer, so one yield loads both (see
     * exec_struct_param). item -1 (empty slot) is a valid script input and is
     * never awaited — the param default answers it. */
    if( (!obj && request.item_id >= 0) || (!param && request.param_id >= 0) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_OC_PARAM;
        req.u.oc_param = request;
        if( !rs_cs2_await_spent(host, req.kind, request.item_id, request.param_id) )
            return rs_cs2_yield_load(host, &req, request.item_id, request.param_id);
        /* Still missing after the load: complete with whatever did arrive. */
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
        if( !rs_cs2_await_spent(host, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, &req, request.item_id, -1);
        /* Objtype still missing after its load: answer like the empty slot. */
        return CS2VM2_PushInt(thread, 0);
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
        if( !rs_cs2_await_spent(host, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, &req, request.item_id, -1);
        /* Objtype still missing after its load: the reference "null" name. */
        return CS2VM2_PushStr(thread, strdup(name));
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
        if( !rs_cs2_await_spent(host, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, &req, request.item_id, -1);
        /* Objtype still missing after its load: pass the id through unresolved. */
        return CS2VM2_PushInt(thread, request.item_id);
    }
}

/* OC_OP/OC_IOP: ground/inventory right-click action string at a menu slot
 * (op_index 0..4). Real data, following the exact OC_NAME yield-on-miss shape. */
static int
exec_oc_op(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Op request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Objtype* obj =
        provider ? CacheProvider_ObjtypeGet(provider, request.item_id) : NULL;

    if( request.item_id < 0 )
        return CS2VM2_PushStr(thread, strdup(""));

    if( !obj )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = request.opcode == CS2_OP_OC_IOP ? CS2VM_HOST_REQUEST_OC_IOP
                                                    : CS2VM_HOST_REQUEST_OC_OP;
        req.u.oc_op = request;
        if( !rs_cs2_await_spent(host, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, &req, request.item_id, -1);
        /* Objtype still missing after its load: no action string to give. */
        return CS2VM2_PushStr(thread, strdup(""));
    }

    if( request.op_index < 0 || request.op_index >= TORIRS_MENU_ACTION_SLOTS )
        return CS2VM2_PushStr(thread, strdup(""));

    char const* action = request.opcode == CS2_OP_OC_IOP
                              ? obj->inv_actions[request.op_index]
                              : obj->ground_actions[request.op_index];
    return CS2VM2_PushStr(thread, strdup(action ? action : ""));
}

/* OC_EXAMINE: real data (ToriRS_Objtype.desc), following the OC_NAME shape. */
static int
exec_oc_examine(
    struct RS_CS2Host* host,
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Name request)
{
    struct CacheProvider* provider = rs_cs2_provider(host);
    struct ToriRS_Objtype* obj =
        provider ? CacheProvider_ObjtypeGet(provider, request.item_id) : NULL;

    if( request.item_id < 0 )
        return CS2VM2_PushStr(thread, strdup(""));

    if( !obj )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_OC_EXAMINE;
        req.u.oc_examine = request;
        if( !rs_cs2_await_spent(host, req.kind, request.item_id, -1) )
            return rs_cs2_yield_load(host, &req, request.item_id, -1);
        return CS2VM2_PushStr(thread, strdup(""));
    }

    return CS2VM2_PushStr(thread, strdup(obj->desc[0] != '\0' ? obj->desc : ""));
}

/* OC_PLACEHOLDER: identity passthrough, mirroring OC_UNPLACEHOLDER — no
 * placeholder linkage data exists on ToriRS_Objtype yet. */
static int
exec_oc_placeholder(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Unplaceholder request)
{
    return CS2VM2_PushInt(thread, request.item_id);
}

/* OC_FIND/OC_FINDNEXT/OC_FINDRESET: no backing search index exists in this
 * port, so every variant answers "not found". */
static int
exec_oc_find(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Find request)
{
    (void)request;
    return CS2VM2_PushInt(thread, -1);
}

/* OC_SHIFTCLICKIOP: no per-item shift-click preference data exists yet. */
static int
exec_oc_shiftclickiop(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Name request)
{
    (void)request;
    return CS2VM2_PushInt(thread, -1);
}

/* OC_WEARPOS/WEARPOS2/WEARPOS3: no equip slot data exists on ToriRS_Objtype
 * yet, so every variant answers "not equippable". */
static int
exec_oc_wearpos(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_WearPos request)
{
    (void)request;
    return CS2VM2_PushInt(thread, -1);
}

/* OC_WEIGHT: no weight data exists on ToriRS_Objtype yet. */
static int
exec_oc_weight(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Name request)
{
    (void)request;
    return CS2VM2_PushInt(thread, 0);
}

/* oc_isubop(obj, opIndex, subIndex) -> string. No sub-menu nesting exists on
 * ToriRS_Objtype yet. */
static int
exec_oc_isubop(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest_OC_Isubop request)
{
    (void)request;
    return CS2VM2_PushStr(thread, strdup(""));
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
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_CC_SETGRAPHIC;
        req.u.cc_set_graphic = request;
        if( !rs_cs2_await_spent(host, req.kind, request.graphic_id, -1) )
            return rs_cs2_yield_load(host, &req, request.graphic_id, -1);
        /* Sprite still missing after its load: clear the graphic. */
        (void)UITree_ApplyGraphic(tree, request.component_id, -1, 0);
        return CS2VM_EXECNO_OK;
    }

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

    /* The icon needs the objtype, its count variant, the inventory model and
     * that model's textures. Task_ObjModelLoad fetches all of them, so ask it
     * once whether anything is missing rather than yielding per piece. */
    if( !provider || ObjModelLoad_NeedsWork(provider, obj_id, count) )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_CC_SETOBJECT;
        req.u.cc_set_object.component_id = component_id;
        req.u.cc_set_object.obj_id = obj_id;
        req.u.cc_set_object.count = count;
        if( provider && !rs_cs2_await_spent(host, req.kind, obj_id, count) )
            return rs_cs2_yield_load(host, &req, obj_id, count);
        if( !provider )
        {
            (void)UITree_ApplyObject(tree, component_id, obj_id, count, -1, 0);
            return CS2VM_EXECNO_OK;
        }
        /* Still incomplete after the load — a texture that failed, say. Build
         * the icon from what did arrive; the raster skips missing faces. */
    }

    if( host->bridge )
        scene_id = UITreeSceneBridge_EnsureObjIcon(host->bridge, obj_id, count);
    else
        (void)rs_cs2_resolve_obj_icon(host, obj_id, &scene_id, &atlas_index);

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
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_CC_SETTEXTFONT;
        req.u.cc_set_text_font = request;
        if( !rs_cs2_await_spent(host, req.kind, request.font_id, -1) )
            return rs_cs2_yield_load(host, &req, request.font_id, -1);
        /* Font still missing after its load: leave the node without one. */
        (void)UITree_ApplyTextFont(rs_cs2_tree(host), request.component_id, -1);
        return CS2VM_EXECNO_OK;
    }

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

    /* Group is mounted; a parent that still isn't there cannot be loaded in. */
    parent_idx = UITree_FindByComponentId(tree, parent_id);
    if( parent_idx < 0 )
        return CS2VM_EXECNO_OK;

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

    /* Group is mounted; a parent that still isn't there cannot be loaded in. */
    parent_idx = UITree_FindByComponentId(tree, parent_id);
    if( parent_idx < 0 )
        return CS2VM_EXECNO_OK;

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
        /* Group is mounted; an absent parent means not-found, not another load. */
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

    /* Group is mounted; an absent component means not-found, not another load. */
    if( rs_cs2_find_node(host, request.component_id) >= 0 )
    {
        rs_cs2_set_cc_target(vm, request.dot_operand, request.component_id);
        found = 1;
    }

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

    /* Group is mounted; an absent parent simply has no children to iterate. */
    CS2VM2_ResetChildrenIter(vm);
    if( tree )
    {
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
        if( !rs_cs2_await_spent(host, req.kind, request.model_id, -1) )
            return rs_cs2_yield_load(host, &req, request.model_id, -1);
        /* Model still missing after its load: leave the widget as it was. */
        return CS2VM_EXECNO_OK;
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
        if( !rs_cs2_await_spent(host, req.kind, request.model_id, -1) )
            return rs_cs2_yield_load(host, &req, request.model_id, -1);
        /* Model still missing after its load: leave the widget as it was. */
        return CS2VM_EXECNO_OK;
    }
    if( request.model_kind == CS2VM_MODEL_KIND_PLAIN && rs_cs2_tree(host) )
    {
        int scene_model = request.model_id;
        if( host->bridge && scene_model >= 0 )
            scene_model = UITreeSceneBridge_EnsureModel(host->bridge, scene_model);
        (void)UITree_ApplyModel(rs_cs2_tree(host), request.component_id, scene_model);
    }
    /* NPC head (kind 2): request.model_id is the npc id. Composite the chathead
     * (reference IfType.getModel type 2 / NpcType.getHead). Best-effort: applies
     * once the npctype + its head models are resident; the compositor returns -1
     * (widget unchanged) until then. */
    else if( request.model_kind == CS2VM_MODEL_KIND_NPC_HEAD && host->bridge && rs_cs2_tree(host) &&
             request.model_id >= 0 )
    {
        int scene_model = UITreeSceneBridge_EnsureNpcHead(host->bridge, request.model_id);
        if( scene_model >= 0 )
            (void)UITree_ApplyModel(rs_cs2_tree(host), request.component_id, scene_model);
    }
    /* Player head/self/chathead (kinds 3/5/6): composite the local appearance
     * head (reference IfType.getModel type 3 / ClientPlayer.getHeadModel). */
    else if( (request.model_kind == CS2VM_MODEL_KIND_PLAYER_HEAD ||
              request.model_kind == CS2VM_MODEL_KIND_PLAYER_SELF ||
              request.model_kind == CS2VM_MODEL_KIND_PLAYER_CHATHEAD) &&
             host->bridge && rs_cs2_tree(host) )
    {
        /* The CS2 host has no world handle, so it can only bind an already
         * composited player head (cache hit). The IF1 IF_SETPLAYERHEAD path
         * (App-driven) is what composites it from the real appearance. */
        int scene_model = UITreeSceneBridge_EnsurePlayerHead(host->bridge, NULL, NULL, 0);
        if( scene_model >= 0 )
            (void)UITree_ApplyModel(rs_cs2_tree(host), request.component_id, scene_model);
    }
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
         * button targets chatbox 162:36). Sub-mount the group; once it is
         * baked, a still-missing child is a no-op (reference tolerates sets on
         * absent widgets). */
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_WIDGET_SET_INT;
        req.u.widget_set_int = request;
        return rs_cs2_yield_if_group_missing(host, request.component_id, &req);
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
    case CS2VM_WIDGET_INT_FORCE_LEFT_CLICK:
        (void)UITree_ApplyForceLeftClick(
            rs_cs2_tree(host), request.component_id, request.value == 1);
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
    case CS2VM_HOST_REQUEST_IF_SETONKEY:
    case CS2VM_HOST_REQUEST_CC_SETONKEY:
        return &node->runtime_hooks.on_key;
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
    case CS2VM_HOST_REQUEST_IF_SETONKEY:
        return "IF_SETONKEY";
    case CS2VM_HOST_REQUEST_CC_SETONKEY:
        return "CC_SETONKEY";
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

static int
rs_cs2_host_exec_dispatch(
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest* request);

int
RS_CS2Host_Exec(
    struct CS2VM2_Thread* vm,
    struct CS2VM_HostRequest* request)
{
    struct RS_CS2Host* host;
    int result;

    assert(vm);
    assert(request);

    host = (struct RS_CS2Host*)CS2VM_USER(vm);
    assert(host && "CS2VM_USER(thread) must be RS_CS2Host*");

    result = rs_cs2_host_exec_dispatch(vm, request);
    /* The await record only spans the yield -> load -> retry window: a request
     * that completes retires it, so a resource evicted later can be awaited
     * again. */
    if( result != CS2VM_EXECNO_YIELD )
        host->has_awaited = false;
    return result;
}

static int
rs_cs2_host_exec_dispatch(
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
        int value = host->varcs ? VarCManager_GetInt(host->varcs, id) : 0;
        return CS2VM2_PushInt(vm, value);
    }

    case CS2VM_HOST_REQUEST_KEYHELD:
    case CS2VM_HOST_REQUEST_KEYPRESSED:
    {
        int key_code = request->u.key_query.key_code;
        unsigned char const* state = request->kind == CS2VM_HOST_REQUEST_KEYHELD
                                         ? host->osrs_key_held
                                         : host->osrs_key_pressed;
        int value = 0;
        if( key_code >= 0 && key_code < TORIRS_OSRSKEY_COUNT )
            value = state[key_code] ? 1 : 0;
        return CS2VM2_PushInt(vm, value);
    }

    case CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING:
    {
        int id = request->u.vars_read_varc_string.varc_id;
        char const* value = host->varcs ? VarCManager_GetString(host->varcs, id) : "";
        return CS2VM2_PushStr(vm, strdup(value));
    }

    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_INT:
    {
        int id = request->u.vars_write_varc_int.varc_id;
        /* The manager fires its change callback (RS_CS2Host_NotifyVarChanged) on a
         * real change, which flags a var-transmit re-dispatch for the tick. */
        if( host->varcs )
            VarCManager_SetInt(host->varcs, id, request->u.vars_write_varc_int.value);
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_STRING:
    {
        int id = request->u.vars_write_varc_string.varc_id;
        if( host->varcs )
            VarCManager_SetString(host->varcs, id, request->u.vars_write_varc_string.value);
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

    case CS2VM_HOST_REQUEST_OC_OP:
        return exec_oc_op(host, vm, request->u.oc_op);

    case CS2VM_HOST_REQUEST_OC_IOP:
        return exec_oc_op(host, vm, request->u.oc_iop);

    case CS2VM_HOST_REQUEST_OC_EXAMINE:
        return exec_oc_examine(host, vm, request->u.oc_examine);

    case CS2VM_HOST_REQUEST_OC_PLACEHOLDER:
        return exec_oc_placeholder(vm, request->u.oc_placeholder);

    case CS2VM_HOST_REQUEST_OC_FIND:
        return exec_oc_find(vm, request->u.oc_find);

    case CS2VM_HOST_REQUEST_OC_SHIFTCLICKIOP:
        return exec_oc_shiftclickiop(vm, request->u.oc_shiftclickiop);

    case CS2VM_HOST_REQUEST_OC_WEARPOS:
        return exec_oc_wearpos(vm, request->u.oc_wearpos);

    case CS2VM_HOST_REQUEST_OC_WEIGHT:
        return exec_oc_weight(vm, request->u.oc_weight);

    case CS2VM_HOST_REQUEST_OC_ISUBOP:
        return exec_oc_isubop(vm, request->u.oc_isubop);

    case CS2VM_HOST_REQUEST_OC_PARAM:
        return exec_oc_param(host, vm, request->u.oc_param);

    case CS2VM_HOST_REQUEST_PARAHEIGHT:
        return exec_para_height(host, vm, request->u.para_height, 0);

    case CS2VM_HOST_REQUEST_PARAWIDTH:
        return exec_para_height(host, vm, request->u.para_height, 1);

    case CS2VM_HOST_REQUEST_CLIENTCLOCK:
        return CS2VM2_PushInt(vm, host->client_clock);

    case CS2VM_HOST_REQUEST_CAM_SETFOLLOWHEIGHT:
        host->cam_follow_height = request->u.cam_set_follow_height.height;
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CAM_GETFOLLOWHEIGHT:
        return CS2VM2_PushInt(vm, host->cam_follow_height);

    case CS2VM_HOST_REQUEST_HIGHLIGHT:
        /* HIGHLIGHT_* (7000..7037): no highlight-overlay system in this port yet.
         * The request carries the opcode and its popped args (request->u.highlight)
         * for when the host grows real entity/loc/obj highlight state; for now the
         * GET queries answer "not highlighted" (0) and the rest are no-ops. */
        if( request->u.highlight.query )
            return CS2VM2_PushInt(vm, 0);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CLIENTOP:
        /* CLIENTOP_* (6700..6709): no enhanced client-owned context-menu system
         * yet. Args are already popped into request->u.clientop (slot / script_id
         * / label); discard and continue so scripts wiring interface state do not
         * abort. */
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_MINIMENU:
        return exec_minimenu(host, vm, request->u.minimenu.opcode);

    case CS2VM_HOST_REQUEST_CLIENT_OPTION:
        return exec_client_option(host, vm, request->u.client_option);

    case CS2VM_HOST_REQUEST_MINIMAP:
        return exec_minimap(host, vm, request->u.minimap);

    case CS2VM_HOST_REQUEST_LOGOUT:
        host->logout_requested = true;
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_VIEWPORT:
        return exec_viewport(host, vm, request->u.viewport);

    case CS2VM_HOST_REQUEST_UIZOOM:
        return exec_uizoom(host, vm, request->u.uizoom);

    case CS2VM_HOST_REQUEST_SAFEAREA:
        return exec_safearea(vm, request->u.safearea);

    case CS2VM_HOST_REQUEST_CAM_GETYAW:
        return CS2VM2_PushInt(vm, host->cam_yaw);

    case CS2VM_HOST_REQUEST_WORLDMAP:
        return exec_worldmap(host, vm, request->u.worldmap);

    case CS2VM_HOST_REQUEST_MEC:
        return exec_mec(host, vm, request->u.mec);

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

    case CS2VM_HOST_REQUEST_IF_HASSUB:
    {
        /* A component "has a sub" when an interface group is mounted into it
         * (IF_OPENSUB target). The InterfaceParent map records exactly that. */
        int cid = request->u.if_get_width.component_id;
        int has = tree && UITree_InterfaceParentFind(tree, cid) >= 0;
        if( getenv("TORIRS_HASSUB_DEBUG") )
            fprintf(
                stderr,
                "hassub: query 0x%08x (%d|%d) -> %d  (parent_count=%d)\n",
                (unsigned)cid, (cid >> 16) & 0xffff, cid & 0xffff, has,
                tree ? tree->interface_parent_count : -1);
        return CS2VM2_PushInt(vm, has ? 1 : 0);
    }

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
            if( getenv("TORIRS_SETHIDE_DEBUG") )
            {
                int g = (request->u.if_set_hide.component_id >> 16) & 0xffff;
                if( g == 149 || g == 320 || (g == 161 && (request->u.if_set_hide.component_id & 0xffff) >= 73) )
                    fprintf(
                        stderr,
                        "sethide: component 0x%08x (%d|%d) hide=%d\n",
                        (unsigned)request->u.if_set_hide.component_id,
                        g,
                        request->u.if_set_hide.component_id & 0xffff,
                        request->u.if_set_hide.hidden ? 1 : 0);
            }
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

    case CS2VM_HOST_REQUEST_IF_CLEAROPSUBMENU:
        if( tree )
            (void)UITree_ClearOpSubmenu(
                tree,
                request->u.if_clear_op_submenu.component_id,
                request->u.if_clear_op_submenu.op_index);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_IF_SETTARGETPRIORITY:
        if( tree )
            (void)UITree_ApplyTargetPriority(
                tree,
                request->u.if_set_target_priority.component_id,
                request->u.if_set_target_priority.priority);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_WIDGET_SET_OPKEY:
        if( tree )
            (void)UITree_ApplyOpKey(
                tree,
                request->u.widget_set_opkey.component_id,
                request->u.widget_set_opkey.op_index,
                request->u.widget_set_opkey.key_chars,
                request->u.widget_set_opkey.key_codes,
                request->u.widget_set_opkey.pair_count);
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_WIDGET_SET_OPKEY_RATE:
        if( tree )
        {
            if( request->u.widget_set_opkey_rate.ignore_held )
                (void)UITree_ApplyOpKeyIgnoreHeld(
                    tree,
                    request->u.widget_set_opkey_rate.component_id,
                    request->u.widget_set_opkey_rate.op_index);
            else
                (void)UITree_ApplyOpKeyRate(
                    tree,
                    request->u.widget_set_opkey_rate.component_id,
                    request->u.widget_set_opkey_rate.op_index,
                    request->u.widget_set_opkey_rate.rate,
                    request->u.widget_set_opkey_rate.enabled);
        }
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
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
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
    case CS2VM_HOST_REQUEST_CC_SETONKEY:
        return exec_set_on_cc_event(host, vm, request->kind, &request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_CC_SETONVARTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONINVTRANSMIT:
        return exec_set_on_cc_transmit(host, vm, request->kind, &request->u.cc_set_on_op);
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
