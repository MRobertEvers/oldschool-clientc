#include "game_interface_editor_cs2_host.h"

#include "interface_editor.h"

#include "buildcache/dat2_buildcache.h"
#include "games/ie_enum_lookup.h"
#include "games/ie_struct_lookup.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toriauxlib/vm/toriauxlibvm.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_scene.h"
#include "toriauxlib/cache/toriauxlibcache_clientscript_convert.h"
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
ie_cs2h_tree(struct GameInterfaceEditorCS2Host* host)
{
    return host && host->game ? host->game->cs2_tree : NULL;
}

static struct ToriAuxLibCore*
ie_cs2h_core(struct GameInterfaceEditorCS2Host* host)
{
    return host && host->game ? host->game->core : NULL;
}

static struct ToriDraw_Scene*
ie_cs2h_scene(struct GameInterfaceEditorCS2Host* host)
{
    return host && host->game ? host->game->scene : NULL;
}

static struct ToriAuxLibVM*
ie_cs2h_vm(struct GameInterfaceEditorCS2Host* host)
{
    return host && host->game ? host->game->cs1vm_wrap : NULL;
}

static struct ToriAuxLibCache*
ie_cs2h_cache(struct GameInterfaceEditorCS2Host* host)
{
    assert(host);
    if( host->game->cache )
        return host->game->cache;
    if( host->game->td )
        return ToriAuxLibTD_C(host->game->td);
    return NULL;
}

static struct Dat2BuildCache*
ie_cs2h_dat2(struct GameInterfaceEditorCS2Host* host)
{
    struct ToriAuxLibCache* cache = ie_cs2h_cache(host);
    return cache ? dat2(cache) : NULL;
}

static struct UITreeScrollState
ie_cs2h_scroll_view(struct GameInterfaceEditorCS2Host* host)
{
    struct UITreeScrollState view = { 0 };
    (void)host;
    /* Editor has no live ui_scroll runtime; getters return 0. */
    return view;
}

static int32_t
ie_cs2h_find_node(
    struct GameInterfaceEditorCS2Host* host,
    int component_id)
{
    struct UITree* tree = ie_cs2h_tree(host);
    assert(tree);
    if( component_id < 0 )
        return -1;
    return uitree_find_by_component_id(tree, component_id);
}

static struct StaticUIComponent*
ie_cs2h_node(
    struct GameInterfaceEditorCS2Host* host,
    int component_id)
{
    struct UITree* tree = ie_cs2h_tree(host);
    int32_t idx = ie_cs2h_find_node(host, component_id);
    assert(tree);
    if( idx < 0 )
        return NULL;
    return &tree->components[idx];
}

/** Editor has no async yield pump — record pending for diagnostics and soft-fail. */
static int
ie_cs2h_soft_fail(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VM_HostRequest const* request)
{
    assert(host);
    assert(request);
    host->pending = *request;
    host->has_pending = true;
    fprintf(
        stderr,
        "GameInterfaceEditor_CS2HostExec: soft-fail kind %d (no async pump)\n",
        (int)request->kind);
    return CS2VM_EXECNO_OK;
}

/** Soft-fail when an interface group/component is not in the UITree yet. */
static int
ie_cs2h_soft_fail_if_group_missing(
    struct GameInterfaceEditorCS2Host* host,
    int component_id,
    struct CS2VM_HostRequest const* request)
{
    int group_id;

    assert(host);
    assert(request);

    group_id = (component_id >> 16) & 0xffff;
    if( component_id <= 0 || group_id <= 0 )
        return CS2VM_EXECNO_OK;

    if( ie_cs2h_find_node(host, component_id) >= 0 )
        return CS2VM_EXECNO_OK;

    (void)ie_cs2h_soft_fail(host, request);
    return CS2VM_EXECNO_ERROR;
}

static bool
ie_cs2h_sprite_ready(
    struct GameInterfaceEditorCS2Host* host,
    int graphic_id)
{
    struct ToriDraw_Scene* scene;
    struct ToriAuxLibCore* core;

    if( graphic_id < 0 )
        return true;

    scene = ie_cs2h_scene(host);
    if( scene && ToriDraw_SceneSpriteHas(scene, graphic_id) )
        return true;

    core = ie_cs2h_core(host);
    if( core && ToriAuxLibCore_SpriteHas(core, graphic_id) )
    {
        /* In core but not scene — task layer must promote; treat as not ready. */
        return false;
    }
    return false;
}

static bool
ie_cs2h_font_ready(
    struct GameInterfaceEditorCS2Host* host,
    int font_id)
{
    struct ToriDraw_Scene* scene = ie_cs2h_scene(host);
    if( font_id < 0 )
        return true;
    if( scene && ToriDraw_SceneFontGet(scene, font_id) )
        return true;
    return false;
}

static bool
ie_cs2h_model_ready(
    struct GameInterfaceEditorCS2Host* host,
    int model_id)
{
    struct ToriAuxLibCore* core = ie_cs2h_core(host);
    if( model_id < 0 )
        return true;
    return core && ToriAuxLibCore_ModelHas(core, model_id);
}

static bool
ie_cs2h_resolve_obj_icon(
    struct GameInterfaceEditorCS2Host* host,
    int obj_id,
    int* out_scene_id,
    int* out_atlas_index)
{
    struct GameInterfaceEditor* game;

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
ie_cs2h_apply_op(
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
ie_cs2h_set_cc_target(
    struct CS2VMX* vm,
    int dot_operand,
    int component_id)
{
    CS2VMX_SetTargetComponentId(vm, dot_operand, component_id);
}

static int
ie_cs2h_collect_dynamic_children(
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
ie_cs2h_parent_component_id(
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
GameInterfaceEditor_CS2HostInit(
    struct GameInterfaceEditorCS2Host* host,
    struct GameInterfaceEditor* game)
{
    assert(host);

    memset(host, 0, sizeof(*host));
    host->game = game;
    host->client_clock = 100;
    host->client_type = 80;
    if( game )
    {
        host->viewport_w = game->preview_layout_w > 0 ? game->preview_layout_w : IE_PREVIEW_ROOT_W;
        host->viewport_h = game->preview_layout_h > 0 ? game->preview_layout_h : IE_PREVIEW_ROOT_H;
    }
}

void
GameInterfaceEditor_CS2HostTick(struct GameInterfaceEditorCS2Host* host)
{
    assert(host);

    host->client_clock++;
}

/* =========================================================================
 * Inventory
 * ========================================================================= */

static int
ie_cs2h_inv_size(struct GameInterfaceEditorCS2Host* host, int inv_id)
{
    struct RSInvContainer const* container;
    assert(host);
    if( !host->game || inv_id < 0 )
        return 0;
    container = rs_inv_container_find(&host->game->inv_data.store, inv_id);
    if( container )
        return container->slot_count;
    switch( inv_id )
    {
    case 93:
        return 28;
    case 94:
        return 14;
    case 95:
        return 800;
    case 516:
        return 40;
    case 620:
        return 500;
    default:
        return 0;
    }
}

static int
ie_cs2h_inv_get_obj(struct GameInterfaceEditorCS2Host* host, int inv_id, int slot)
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
ie_cs2h_inv_get_num(struct GameInterfaceEditorCS2Host* host, int inv_id, int slot)
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
ie_cs2h_inv_total(struct GameInterfaceEditorCS2Host* host, int inv_id, int item_id)
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

static struct CS2_Script*
ie_cs2h_find_cached_script(
    struct GameInterfaceEditorCS2Host* host,
    int script_id)
{
    struct GameInterfaceEditor* game;
    assert(host);
    if( !host->game || script_id < 0 )
        return NULL;
    game = host->game;
    for( int i = 0; i < game->script_cache_count; i++ )
    {
        if( game->script_cache[i].script_id == script_id && game->script_cache[i].loaded )
            return &game->script_cache[i].loaded->script;
    }
    return NULL;
}

static struct CS2_Script*
ie_cs2h_load_script_sync(
    struct GameInterfaceEditorCS2Host* host,
    int script_id)
{
    struct GameInterfaceEditor* game;
    struct RSCacheDat2Disk* disk;
    struct RSCacheDat2Disk_Archive* archive;
    struct ToriAuxLibCore_ClientScript* loaded;
    struct InterfaceEditorScriptEntry* entry;

    game = host ? host->game : NULL;
    assert(game);
    if( script_id < 0 )
        return NULL;

    {
        struct CS2_Script* existing = ie_cs2h_find_cached_script(host, script_id);
        if( existing )
            return existing;
    }

    disk = game->dat2_cache;
    if( !disk || game->script_cache_count >= IE_SCRIPT_CACHE_MAX )
        return NULL;

    archive = RSCacheDat2Disk_ArchiveNewLoad(disk, RSCacheDat2Disk_Table_Clientscript, script_id);
    assert(archive);

    loaded = ToriAuxLibCache_ClientScriptNewFromDat2Archive2(
        archive, script_id, game->clientscript_decode_flags);
    if( !loaded || loaded->script.op_count <= 0 )
    {
        if( loaded )
        {
            cs2_script_free(&loaded->script);
            free(loaded);
        }
        return NULL;
    }

    entry = &game->script_cache[game->script_cache_count++];
    entry->script_id = script_id;
    entry->loaded = loaded;
    return &loaded->script;
}

static int
exec_push_script(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    int script_id)
{
    struct ToriAuxLibCore* core = ie_cs2h_core(host);
    struct ToriAuxLibCore_ClientScript* cs = NULL;
    struct CS2_Script* script = NULL;

    if( core )
        cs = ToriAuxLibCore_ClientScriptGet(core, script_id);
    if( cs )
        return CS2VMX_PushCallScript(vm, &cs->script);

    script = ie_cs2h_load_script_sync(host, script_id);
    if( script )
        return CS2VMX_PushCallScript(vm, script);

    fprintf(stderr, "GameInterfaceEditor_CS2HostExec: unresolved gosub script %d\n", script_id);
    if( host && host->game )
        host->game->diag_gosub_unresolved++;
    return CS2VM_EXECNO_ERROR;
}

static int
exec_para_height(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_ParaHeight request,
    int is_width)
{
    int result = 0;
    char const* text = request.text ? request.text : "";
    if( text[0] != '\0' )
    {
        struct ToriDraw_Font* font =
            ie_cs2h_scene(host) ? ToriDraw_SceneFontGet(ie_cs2h_scene(host), request.font_id) : NULL;
        if( font )
        {
            result = is_width ? ToriDraw2D_WrapMaxLineWidth(font, text, request.max_width)
                              : ToriDraw2D_WrapLineCount(font, text, request.max_width);
        }
    }
    return CS2VMX_PushInt(vm, result);
}

static int
exec_vars_read_varp(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    int varp_id)
{
    int value = 0;
    struct ToriAuxLibVM* aux = ie_cs2h_vm(host);
    if( aux )
        value = ToriAuxLibVM_GetVarp(aux, varp_id);
    return CS2VMX_PushInt(vm, value);
}

static int
exec_vars_read_varbit(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    int varbit_id)
{
    int value = 0;
    struct ToriAuxLibVM* aux = ie_cs2h_vm(host);
    if( aux )
        value = ToriAuxLibVM_GetVarbit(aux, varbit_id);
    return CS2VMX_PushInt(vm, value);
}

static int
exec_enum_lookup(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_EnumLookup request)
{
    struct Dat2BuildCache* bc = ie_cs2h_dat2(host);
    if( !bc || !dat2_buildcache_enum_get(bc, request.enum_id) )
    {
        if( request.output_type == (int)'s' )
            return CS2VMX_PushStr(vm, strdup("null"));
        return CS2VMX_PushInt(vm, 0);
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
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_EnumGetOutputCount request)
{
    struct Dat2BuildCache* bc = ie_cs2h_dat2(host);
    assert(bc);
    if( !dat2_buildcache_enum_get(bc, request.enum_id) )
        return CS2VMX_PushInt(vm, 0);
    return CS2VMX_PushInt(vm, ie_enum_output_count(bc, request.enum_id));
}

static int
exec_struct_param(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_StructParam request)
{
    bool is_string = false;
    int intval = 0;
    char const* strval = NULL;
    bool found;
    struct Dat2BuildCache* bc = ie_cs2h_dat2(host);

    assert(bc);
    if( !dat2_buildcache_struct_get(bc, request.struct_id) )
        return CS2VMX_PushInt(vm, 0);

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
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_OC_IntParam request)
{
    struct Dat2BuildCache* bc = ie_cs2h_dat2(host);
    struct RSCacheDat2A_ConfigObject* obj = bc ? dat2_buildcache_object_get(bc, request.item_id) : NULL;
    int value = 0;

    if( !obj )
    {
        struct ToriAuxLibCore_Objtype* core_obj =
            ie_cs2h_core(host) ? ToriAuxLibCore_ObjtypeGet(ie_cs2h_core(host), request.item_id) : NULL;
        if( !core_obj )
            return CS2VMX_PushInt(vm, 0);
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
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_OC_Name request)
{
    struct ToriAuxLibCore* core = ie_cs2h_core(host);
    struct ToriAuxLibCore_Objtype* obj = core ? ToriAuxLibCore_ObjtypeGet(core, request.item_id) : NULL;
    char const* name = "null";

    if( !obj )
    {
        struct Dat2BuildCache* bc = ie_cs2h_dat2(host);
        struct RSCacheDat2A_ConfigObject* cfg =
            bc ? dat2_buildcache_object_get(bc, request.item_id) : NULL;
        if( !cfg )
            return CS2VMX_PushStr(vm, strdup("null"));
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
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_OC_Unplaceholder request)
{
    (void)host;
    /* Editor soft-fail: treat as identity when config is missing. */
    return CS2VMX_PushInt(vm, request.item_id);
}

static int
exec_set_graphic(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetGraphic request)
{
    struct UITree* tree = ie_cs2h_tree(host);
    (void)vm;

    assert(tree);

    /* Apply even if sprite is not scene-ready yet (editor soft-fail). */
    (void)uitree_apply_graphic(tree, request.component_id, request.graphic_id);
    return CS2VM_EXECNO_OK;
}

static int
exec_set_object(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    int component_id,
    int obj_id,
    int count)
{
    struct UITree* tree = ie_cs2h_tree(host);
    int scene_id = -1;
    int atlas_index = 0;
    (void)vm;

    assert(tree);

    if( obj_id <= 0 )
    {
        (void)uitree_apply_object(tree, component_id, 0, 0, -1, 0);
        return CS2VM_EXECNO_OK;
    }

    (void)ie_cs2h_resolve_obj_icon(host, obj_id, &scene_id, &atlas_index);
    (void)uitree_apply_object(tree, component_id, obj_id, count, scene_id, atlas_index);
    return CS2VM_EXECNO_OK;
}

static int
exec_set_text_font(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetTextFont request)
{
    struct StaticUIComponent* node;
    (void)vm;

    node = ie_cs2h_node(host, request.component_id);
    if( node && node->type == UIELEM_RS_TEXT )
    {
        node->u.rs_text.font_id = request.font_id;
        uitree_mark_node_dirty(ie_cs2h_tree(host), ie_cs2h_find_node(host, request.component_id));
    }
    return CS2VM_EXECNO_OK;
}

static int
exec_cc_create(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_Create request)
{
    struct UITree* tree = ie_cs2h_tree(host);
    int parent_id = request.parent_id;
    int32_t parent_idx;
    int32_t child_idx;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };

    yield_req.kind = CS2VM_HOST_REQUEST_CC_CREATE;
    yield_req.u.cc_create = request;
    yield_res = ie_cs2h_soft_fail_if_group_missing(host, parent_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
    {
        if( host->game )
            host->game->diag_cc_create_failed++;
        return CS2VM_EXECNO_OK;
    }

    if( !tree )
    {
        if( host->game )
            host->game->diag_cc_create_failed++;
        return CS2VM_EXECNO_OK;
    }

    parent_idx = uitree_find_by_component_id(tree, parent_id);
    if( parent_idx < 0 )
    {
        if( host->game )
            host->game->diag_cc_create_failed++;
        if( parent_id > 0 )
            (void)ie_cs2h_soft_fail(host, &yield_req);
        return CS2VM_EXECNO_OK;
    }

    child_idx = uitree_cc_create(
        tree, parent_idx, parent_id, request.component_type, request.child_index);
    if( child_idx < 0 )
    {
        if( host->game )
            host->game->diag_cc_create_failed++;
        return CS2VM_EXECNO_ERROR;
    }

    if( host->game )
        host->game->diag_cc_create_ok++;
    ie_cs2h_set_cc_target(vm, request.dot_operand, tree->components[child_idx].component_id);
    return CS2VM_EXECNO_OK;
}

static int
exec_cc_find(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_Find request)
{
    struct UITree* tree = ie_cs2h_tree(host);
    int found = 0;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };
    int32_t parent_idx;

    yield_req.kind = CS2VM_HOST_REQUEST_CC_FIND;
    yield_req.u.cc_find = request;
    yield_res = ie_cs2h_soft_fail_if_group_missing(host, request.parent_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
        return CS2VMX_PushInt(vm, 0);

    if( tree )
    {
        parent_idx = uitree_find_by_component_id(tree, request.parent_id);
        if( parent_idx >= 0 )
        {
            int32_t child_idx = uitree_find_child_by_subid(
                tree, parent_idx, request.parent_id, request.sub_id);
            if( child_idx >= 0 )
            {
                ie_cs2h_set_cc_target(vm, request.dot_operand, tree->components[child_idx].component_id);
                found = 1;
            }
        }
        else if( request.parent_id > 0 )
            (void)ie_cs2h_soft_fail(host, &yield_req);
    }

    return CS2VMX_PushInt(vm, found);
}

static int
exec_if_find(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_TargetFind request)
{
    int found = 0;
    int yield_res;
    struct CS2VM_HostRequest yield_req = { 0 };

    yield_req.kind = CS2VM_HOST_REQUEST_IF_FIND;
    yield_req.u.if_find = request;
    yield_res = ie_cs2h_soft_fail_if_group_missing(host, request.component_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
        return CS2VMX_PushInt(vm, 0);

    if( ie_cs2h_find_node(host, request.component_id) >= 0 )
    {
        ie_cs2h_set_cc_target(vm, request.dot_operand, request.component_id);
        found = 1;
    }
    else if( request.component_id > 0 )
        (void)ie_cs2h_soft_fail(host, &yield_req);

    return CS2VMX_PushInt(vm, found);
}

static int
exec_children_find(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    int parent_id,
    int start_index,
    int set_target_dot,
    int dot_operand,
    enum CS2VM_HostRequestKind kind)
{
    struct UITree* tree = ie_cs2h_tree(host);
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

    yield_res = ie_cs2h_soft_fail_if_group_missing(host, parent_id, &yield_req);
    if( yield_res != CS2VM_EXECNO_OK )
    {
        CS2VMX_ResetChildrenIter(vm);
        return CS2VM_EXECNO_OK;
    }

    CS2VMX_ResetChildrenIter(vm);
    if( tree )
    {
        if( uitree_find_by_component_id(tree, parent_id) < 0 && parent_id > 0 )
        {
            (void)ie_cs2h_soft_fail(host, &yield_req);
            return CS2VM_EXECNO_OK;
        }

        vm->children_iter_count = ie_cs2h_collect_dynamic_children(
            tree,
            parent_id,
            start_index,
            vm->children_iter_indices,
            CS2VMX_CHILDREN_ITER_MAX);

        if( set_target_dot && uitree_find_by_component_id(tree, parent_id) >= 0 )
            ie_cs2h_set_cc_target(vm, dot_operand, parent_id);
    }
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_model(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_WidgetSetModel request)
{
    (void)vm;
    if( ie_cs2h_tree(host) )
        (void)uitree_apply_model(ie_cs2h_tree(host), request.component_id, request.model_id);
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_model_kind(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_WidgetSetModelKind request)
{
    (void)vm;
    if( request.model_kind == CS2VM_MODEL_KIND_PLAIN && ie_cs2h_tree(host) )
        (void)uitree_apply_model(ie_cs2h_tree(host), request.component_id, request.model_id);
    else if( ie_cs2h_tree(host) && request.model_id >= 0 )
        (void)uitree_apply_model(ie_cs2h_tree(host), request.component_id, request.model_id);
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_int(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_WidgetSetInt request)
{
    struct StaticUIComponent* node = ie_cs2h_node(host, request.component_id);
    int32_t idx;
    (void)vm;
    assert(node);

    idx = ie_cs2h_find_node(host, request.component_id);

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
        (void)uitree_apply_colour(ie_cs2h_tree(host), request.component_id, request.value);
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
        (void)uitree_apply_click_mask(ie_cs2h_tree(host), request.component_id, request.value);
        break;
    case CS2VM_WIDGET_INT_DRAG_DEAD_ZONE:
        node->drag_dead_zone = (uint8_t)request.value;
        break;
    case CS2VM_WIDGET_INT_DRAG_DEAD_TIME:
        node->drag_dead_time = (uint8_t)request.value;
        break;
    case CS2VM_WIDGET_INT_MODEL_TRANSPARENT:
        (void)uitree_apply_model_transparent(
            ie_cs2h_tree(host), request.component_id, request.value);
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
        uitree_mark_node_dirty(ie_cs2h_tree(host), idx);
    return CS2VM_EXECNO_OK;
}

static int
exec_widget_set_model_angle(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_WidgetSetModelAngle request)
{
    struct StaticUIComponent* node = ie_cs2h_node(host, request.component_id);
    (void)vm;
    assert(node);
    if( node->type != UIELEM_RS_MODEL )
        return CS2VM_EXECNO_OK;
    node->u.rs_model.xan = request.angle_x;
    node->u.rs_model.yan = request.angle_y;
    if( request.zoom > 0 )
        node->u.rs_model.zoom = request.zoom;
    uitree_mark_node_dirty(ie_cs2h_tree(host), ie_cs2h_find_node(host, request.component_id));
    return CS2VM_EXECNO_OK;
}

static void
ie_cs2h_store_runtime_hook(
    struct GameInterfaceEditor* game,
    int target_component_id,
    enum InterfaceEditorSetonEvent event,
    int script_id,
    int argc,
    int const* argv,
    int trigger_count,
    int const* triggers)
{
    assert(game);
    if( target_component_id < 0 || event < 0 || event >= IE_SETON_EVENT_COUNT )
        return;

    if( script_id < 0 )
    {
        for( int i = 0; i < game->runtime_hook_count; i++ )
        {
            struct InterfaceEditorRuntimeHook* hook = &game->runtime_hooks[i];
            assert(hook);
            if( hook->target_component_id != target_component_id || hook->event != event )
                continue;
            for( int j = i + 1; j < game->runtime_hook_count; j++ )
                game->runtime_hooks[j - 1] = game->runtime_hooks[j];
            game->runtime_hook_count--;
            game->runtime_hooks[game->runtime_hook_count].used = false;
            return;
        }
        return;
    }

    for( int i = 0; i < game->runtime_hook_count; i++ )
    {
        struct InterfaceEditorRuntimeHook* hook = &game->runtime_hooks[i];
        assert(hook);
        if( hook->target_component_id != target_component_id || hook->event != event )
            continue;
        hook->script_id = script_id;
        hook->argc = argc > 16 ? 16 : argc;
        if( argv && hook->argc > 0 )
            memcpy(hook->argv, argv, (size_t)hook->argc * sizeof(int));
        hook->trigger_count = trigger_count > 8 ? 8 : trigger_count;
        if( triggers && hook->trigger_count > 0 )
            memcpy(hook->triggers, triggers, (size_t)hook->trigger_count * sizeof(int));
        return;
    }

    if( game->runtime_hook_count >= IE_RUNTIME_HOOK_MAX )
        return;

    {
        struct InterfaceEditorRuntimeHook* hook = &game->runtime_hooks[game->runtime_hook_count++];
        memset(hook, 0, sizeof(*hook));
        hook->used = true;
        hook->target_component_id = target_component_id;
        hook->event = event;
        hook->script_id = script_id;
        hook->argc = argc > 16 ? 16 : argc;
        if( argv && hook->argc > 0 )
            memcpy(hook->argv, argv, (size_t)hook->argc * sizeof(int));
        hook->trigger_count = trigger_count > 8 ? 8 : trigger_count;
        if( triggers && hook->trigger_count > 0 )
            memcpy(hook->triggers, triggers, (size_t)hook->trigger_count * sizeof(int));
    }
}

static void
ie_cs2h_store_inv_hook(
    struct GameInterfaceEditor* game,
    int target_component_id,
    int script_id,
    int argc,
    int const* argv,
    int trigger_count,
    int const* triggers)
{
    assert(game);
    if( target_component_id < 0 || script_id < 0 )
        return;

    for( int i = 0; i < game->runtime_inv_hook_count; i++ )
    {
        if( game->runtime_inv_hooks[i].target_component_id != target_component_id )
            continue;
        game->runtime_inv_hooks[i].script_id = script_id;
        game->runtime_inv_hooks[i].argc = argc > 16 ? 16 : argc;
        if( argv && game->runtime_inv_hooks[i].argc > 0 )
            memcpy(
                game->runtime_inv_hooks[i].argv,
                argv,
                (size_t)game->runtime_inv_hooks[i].argc * sizeof(int));
        game->runtime_inv_hooks[i].trigger_count = trigger_count > 8 ? 8 : trigger_count;
        if( triggers && game->runtime_inv_hooks[i].trigger_count > 0 )
            memcpy(
                game->runtime_inv_hooks[i].triggers,
                triggers,
                (size_t)game->runtime_inv_hooks[i].trigger_count * sizeof(int));
        return;
    }

    if( game->runtime_inv_hook_count >= IE_RUNTIME_INV_HOOK_MAX )
        return;

    {
        struct InterfaceEditorRuntimeInvHook* hook =
            &game->runtime_inv_hooks[game->runtime_inv_hook_count++];
        memset(hook, 0, sizeof(*hook));
        hook->target_component_id = target_component_id;
        hook->script_id = script_id;
        hook->argc = argc > 16 ? 16 : argc;
        if( argv && hook->argc > 0 )
            memcpy(hook->argv, argv, (size_t)hook->argc * sizeof(int));
        hook->trigger_count = trigger_count > 8 ? 8 : trigger_count;
        if( triggers && hook->trigger_count > 0 )
            memcpy(hook->triggers, triggers, (size_t)hook->trigger_count * sizeof(int));
    }
}

static void
ie_cs2h_clear_inv_hook(
    struct GameInterfaceEditor* game,
    int target_component_id)
{
    assert(game);

    for( int i = 0; i < game->runtime_inv_hook_count; i++ )
    {
        if( game->runtime_inv_hooks[i].target_component_id != target_component_id )
            continue;
        for( int j = i + 1; j < game->runtime_inv_hook_count; j++ )
            game->runtime_inv_hooks[j - 1] = game->runtime_inv_hooks[j];
        game->runtime_inv_hook_count--;
        return;
    }
}

static enum InterfaceEditorSetonEvent
ie_cs2h_event_from_kind(enum CS2VM_HostRequestKind kind)
{
    switch( kind )
    {
    case CS2VM_HOST_REQUEST_CC_SETONCLICK:
        return IE_SETON_ON_CLICK;
    case CS2VM_HOST_REQUEST_CC_SETONHOLD:
        return IE_SETON_ON_HOLD;
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER:
        return IE_SETON_ON_MOUSEOVER;
    case CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE:
        return IE_SETON_ON_MOUSELEAVE;
    case CS2VM_HOST_REQUEST_CC_SETONDRAG:
        return IE_SETON_ON_DRAG;
    case CS2VM_HOST_REQUEST_CC_SETONSCROLLWHEEL:
        return IE_SETON_ON_SCROLLWHEEL;
    case CS2VM_HOST_REQUEST_CC_SETONKEY:
        return IE_SETON_ON_KEY;
    case CS2VM_HOST_REQUEST_CC_SETONOP:
        return IE_SETON_ON_OP;
    case CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE:
        return IE_SETON_ON_DRAGCOMPLETE;
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEREPEAT:
        return IE_SETON_ON_MOUSEREPEAT;
    case CS2VM_HOST_REQUEST_IF_SETONOP:
        return IE_SETON_ON_OP;
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER:
        return IE_SETON_ON_MOUSEOVER;
    case CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE:
        return IE_SETON_ON_MOUSELEAVE;
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT:
        return IE_SETON_ON_MOUSEREPEAT;
    case CS2VM_HOST_REQUEST_IF_SETONTIMER:
        return IE_SETON_ON_TIMER;
    case CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL:
        return IE_SETON_ON_SCROLLWHEEL;
    case CS2VM_HOST_REQUEST_IF_SETONKEY:
        return IE_SETON_ON_KEY;
    case CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT:
        return IE_SETON_ON_MISCTRANSMIT;
    case CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT:
        return IE_SETON_ON_VARTRANSMIT;
    case CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT:
        return IE_SETON_ON_INVTRANSMIT;
    default:
        return IE_SETON_ON_CLICK;
    }
}

static int
exec_set_on_inv_transmit(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VM_HostRequest_IF_SetOnInvTransmit const* request)
{
    assert(host);
    if( !host->game || !request )
        return CS2VM_EXECNO_OK;

    ie_cs2h_store_runtime_hook(
        host->game,
        request->component_id,
        IE_SETON_ON_INVTRANSMIT,
        request->script_id,
        request->int_arg_count,
        request->int_args,
        request->trigger_count,
        request->trigger_ids);

    if( request->script_id < 0 )
        ie_cs2h_clear_inv_hook(host->game, request->component_id);
    else
        ie_cs2h_store_inv_hook(
            host->game,
            request->component_id,
            request->script_id,
            request->int_arg_count,
            request->int_args,
            request->trigger_count,
            request->trigger_ids);
    return CS2VM_EXECNO_OK;
}

static int
exec_set_on_var_transmit(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VM_HostRequest_IF_SetOnVarTransmit const* request)
{
    assert(host);
    if( !host->game || !request )
        return CS2VM_EXECNO_OK;

    ie_cs2h_store_runtime_hook(
        host->game,
        request->component_id,
        IE_SETON_ON_VARTRANSMIT,
        request->script_id,
        request->int_arg_count,
        request->int_args,
        request->trigger_count,
        request->trigger_ids);
    return CS2VM_EXECNO_OK;
}

static int
exec_set_on_if_event(
    struct GameInterfaceEditorCS2Host* host,
    enum CS2VM_HostRequestKind kind,
    struct CS2VM_HostRequest_IF_SetOnOp const* request)
{
    assert(host);
    if( !host->game || !request )
        return CS2VM_EXECNO_OK;
    ie_cs2h_store_runtime_hook(
        host->game,
        request->component_id,
        ie_cs2h_event_from_kind(kind),
        request->script_id,
        0,
        NULL,
        request->trigger_count,
        request->trigger_ids);
    return CS2VM_EXECNO_OK;
}

static int
exec_set_on_cc_event(
    struct GameInterfaceEditorCS2Host* host,
    struct CS2VMX* vm,
    enum CS2VM_HostRequestKind kind,
    struct CS2VM_HostRequest_CC_SetOnOp const* request)
{
    int target = vm ? vm->active_component_id : -1;
    assert(host);
    if( !host->game || !request )
        return CS2VM_EXECNO_OK;
    ie_cs2h_store_runtime_hook(
        host->game,
        target,
        ie_cs2h_event_from_kind(kind),
        request->script_id,
        0,
        NULL,
        request->trigger_count,
        request->trigger_ids);
    return CS2VM_EXECNO_OK;
}

/* =========================================================================
 * Main dispatcher
 * ========================================================================= */

int
GameInterfaceEditor_CS2HostExec(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request)
{
    struct GameInterfaceEditorCS2Host* host;
    struct UITree* tree;
    struct StaticUIComponent* node;
    struct UITreeScrollState scroll;

    assert(vm);
    if( !request )
        return CS2VM_EXECNO_ERROR;

    host = (struct GameInterfaceEditorCS2Host*)CS2VM_USER(vm);
    if( !host )
    {
        fprintf(stderr, "GameInterfaceEditor_CS2HostExec: CS2VM_USER(vm) is NULL\n");
        return CS2VM_EXECNO_ERROR;
    }

    tree = ie_cs2h_tree(host);
    scroll = ie_cs2h_scroll_view(host);

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_PUSHSCRIPT:
        return exec_push_script(host, vm, request->u.push_script.script_id);

    case CS2VM_HOST_REQUEST_INVS_GET_SIZE:
        return CS2VMX_PushInt(vm, ie_cs2h_inv_size(host, request->u.invs_get_size.inv_id));

    case CS2VM_HOST_REQUEST_INVS_GET_OBJ:
        return CS2VMX_PushInt(
            vm,
            ie_cs2h_inv_get_obj(
                host, request->u.invs_get_obj.inv_id, request->u.invs_get_obj.slot));

    case CS2VM_HOST_REQUEST_INVS_GET_NUM:
        return CS2VMX_PushInt(
            vm,
            ie_cs2h_inv_get_num(
                host, request->u.invs_get_num.inv_id, request->u.invs_get_num.slot));

    case CS2VM_HOST_REQUEST_INVS_GET_TOTAL:
        return CS2VMX_PushInt(
            vm,
            ie_cs2h_inv_total(
                host, request->u.invs_get_total.inv_id, request->u.invs_get_total.item_id));

    case CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR:
        return exec_vars_read_varp(host, vm, request->u.vars_read_varp.varp_id);

    case CS2VM_HOST_REQUEST_VARS_READ_VARBIT:
        return exec_vars_read_varbit(host, vm, request->u.vars_read_varbit.varbit_id);

    case CS2VM_HOST_REQUEST_VARS_READ_VARC_INT:
    {
        int id = request->u.vars_read_varc_int.varc_id;
        int value = 0;
        if( id >= 0 && id < IE_CS2_HOST_VARC_INT_MAX )
            value = host->varc_int[id];
        return CS2VMX_PushInt(vm, value);
    }

    case CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING:
    {
        int id = request->u.vars_read_varc_string.varc_id;
        char* value = (char*)"";
        if( id >= 0 && id < IE_CS2_HOST_VARC_STRING_MAX )
            value = host->varc_string[id];
        return CS2VMX_PushStr(vm, value);
    }

    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_INT:
    {
        int id = request->u.vars_write_varc_int.varc_id;
        if( id >= 0 && id < IE_CS2_HOST_VARC_INT_MAX )
            host->varc_int[id] = request->u.vars_write_varc_int.value;
        return CS2VM_EXECNO_OK;
    }

    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_STRING:
    {
        int id = request->u.vars_write_varc_string.varc_id;
        if( id >= 0 && id < IE_CS2_HOST_VARC_STRING_MAX )
        {
            strncpy(
                host->varc_string[id],
                request->u.vars_write_varc_string.value
                    ? request->u.vars_write_varc_string.value
                    : "",
                IE_CS2_HOST_VARC_STRING_LEN - 1);
            host->varc_string[id][IE_CS2_HOST_VARC_STRING_LEN - 1] = '\0';
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
            "GameInterfaceEditor_CS2HostExec: OC_PARAM not implemented (item=%d param=%d)\n",
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
        node = ie_cs2h_node(host, request->u.if_getx.component_id);
        return CS2VMX_PushInt(vm, node ? node->position.abs_x : 0);

    case CS2VM_HOST_REQUEST_IF_GETY:
        node = ie_cs2h_node(host, request->u.if_get_width.component_id);
        return CS2VMX_PushInt(vm, node ? node->position.abs_y : 0);

    case CS2VM_HOST_REQUEST_IF_GETLAYER:
    {
        int parent = tree ? ie_cs2h_parent_component_id(tree, request->u.if_get_layer.component_id)
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
        node = ie_cs2h_node(host, request->u.if_get_scroll_height.component_id);
        return CS2VMX_PushInt(
            vm, (node && node->type == UIELEM_RS_LAYER) ? node->u.rs_layer.scroll_height : 0);

    case CS2VM_HOST_REQUEST_IF_GETSCROLLWIDTH:
        node = ie_cs2h_node(host, request->u.if_getscrollwidth.component_id);
        return CS2VMX_PushInt(
            vm, (node && node->type == UIELEM_RS_LAYER) ? node->u.rs_layer.scroll_width : 0);

    case CS2VM_HOST_REQUEST_IF_GETHIDE:
        node = ie_cs2h_node(host, request->u.if_get_width.component_id);
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
        node = ie_cs2h_node(host, cid);
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
        node = ie_cs2h_node(host, request->u.cc_set_graphic2.component_id);
        if( node && node->type == UIELEM_RS_GRAPHIC )
        {
            node->u.rs_graphic.scene_id_active = request->u.cc_set_graphic2.graphic_id;
            uitree_mark_node_dirty(
                tree, ie_cs2h_find_node(host, request->u.cc_set_graphic2.component_id));
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
        node = ie_cs2h_node(host, request->u.cc_set_fill.component_id);
        if( node && node->type == UIELEM_RS_RECT )
        {
            node->u.rs_rect.filled = request->u.cc_set_fill.filled ? 1 : 0;
            uitree_mark_node_dirty(
                tree, ie_cs2h_find_node(host, request->u.cc_set_fill.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTRANS:
        node = ie_cs2h_node(host, request->u.cc_set_trans.component_id);
        if( node )
        {
            node->trans = request->u.cc_set_trans.trans;
            uitree_mark_node_dirty(
                tree, ie_cs2h_find_node(host, request->u.cc_set_trans.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETNOCLICKTHROUGH:
        node = ie_cs2h_node(host, request->u.cc_set_no_click_through.component_id);
        if( node )
        {
            node->no_click_through = request->u.cc_set_no_click_through.enabled ? 1 : 0;
            uitree_mark_node_dirty(
                tree, ie_cs2h_find_node(host, request->u.cc_set_no_click_through.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTEXTFONT:
        return exec_set_text_font(host, vm, request->u.cc_set_text_font);

    case CS2VM_HOST_REQUEST_CC_SETTEXTALIGN:
        node = ie_cs2h_node(host, request->u.cc_set_text_align.component_id);
        if( node && node->type == UIELEM_RS_TEXT )
        {
            node->u.rs_text.center = request->u.cc_set_text_align.x_align;
            node->u.rs_text.y_align = request->u.cc_set_text_align.y_align;
            node->u.rs_text.line_height = request->u.cc_set_text_align.line_height;
            uitree_mark_node_dirty(
                tree, ie_cs2h_find_node(host, request->u.cc_set_text_align.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW:
        node = ie_cs2h_node(host, request->u.cc_set_text_shadow.component_id);
        if( node && node->type == UIELEM_RS_TEXT )
        {
            node->u.rs_text.shadowed = request->u.cc_set_text_shadow.shadowed ? 1 : 0;
            uitree_mark_node_dirty(
                tree, ie_cs2h_find_node(host, request->u.cc_set_text_shadow.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLE:
        node = ie_cs2h_node(host, request->u.cc_set_draggable.component_id);
        if( node )
        {
            node->draggable = 1;
            (void)request->u.cc_set_draggable.parent_uid;
            (void)request->u.cc_set_draggable.child_index;
            uitree_mark_node_dirty(
                tree, ie_cs2h_find_node(host, request->u.cc_set_draggable.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLEBEHAVIOR:
        node = ie_cs2h_node(host, request->u.cc_set_draggable_behavior.component_id);
        if( node )
        {
            node->drag_behavior = request->u.cc_set_draggable_behavior.behavior;
            uitree_mark_node_dirty(
                tree, ie_cs2h_find_node(host, request->u.cc_set_draggable_behavior.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADZONE:
        node = ie_cs2h_node(host, request->u.cc_set_drag_dead_zone.component_id);
        if( node )
        {
            node->drag_dead_zone = (uint8_t)request->u.cc_set_drag_dead_zone.zone;
            uitree_mark_node_dirty(
                tree, ie_cs2h_find_node(host, request->u.cc_set_drag_dead_zone.component_id));
        }
        return CS2VM_EXECNO_OK;

    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADTIME:
        node = ie_cs2h_node(host, request->u.cc_set_drag_dead_time.component_id);
        if( node )
        {
            node->drag_dead_time = (uint8_t)request->u.cc_set_drag_dead_time.time;
            uitree_mark_node_dirty(
                tree, ie_cs2h_find_node(host, request->u.cc_set_drag_dead_time.component_id));
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
            tree ? ie_cs2h_parent_component_id(tree, request->u.cc_findroot.component_id) : -1;
        if( parent >= 0 )
        {
            ie_cs2h_set_cc_target(vm, request->u.cc_findroot.dot_operand, parent);
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
            tree ? ie_cs2h_parent_component_id(tree, request->u.cc_resolve_parent.component_id)
                 : -1;
        if( parent < 0 )
            return CS2VM_EXECNO_ERROR;
        return CS2VMX_PushInt(vm, parent);
    }

    case CS2VM_HOST_REQUEST_CC_GETID:
        node = ie_cs2h_node(host, request->u.cc_get_id.component_id);
        assert(node);

        return CS2VMX_PushInt(vm, node->dynamic ? node->dynamic_child_index : -1);

    case CS2VM_HOST_REQUEST_CC_GETX:
        node = ie_cs2h_node(host, request->u.cc_get_id.component_id);
        return CS2VMX_PushInt(vm, node ? node->position.abs_x : 0);

    case CS2VM_HOST_REQUEST_CC_GETY:
        node = ie_cs2h_node(host, request->u.cc_get_id.component_id);
        return CS2VMX_PushInt(vm, node ? node->position.abs_y : 0);

    case CS2VM_HOST_REQUEST_CC_GETWIDTH:
        return CS2VMX_PushInt(
            vm, tree ? uitree_get_layout_width(tree, request->u.cc_get_id.component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETHEIGHT:
        return CS2VMX_PushInt(
            vm, tree ? uitree_get_layout_height(tree, request->u.cc_get_id.component_id) : 0);

    case CS2VM_HOST_REQUEST_CC_GETHIDE:
        node = ie_cs2h_node(host, request->u.cc_get_id.component_id);
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
        node = ie_cs2h_node(host, request->u.cc_gettrans.component_id);
        return CS2VMX_PushInt(vm, node ? node->trans : 0);

    /* ---- Ops ---- */
    case CS2VM_HOST_REQUEST_CC_SETOP:
    case CS2VM_HOST_REQUEST_IF_SETOP:
        if( tree )
            ie_cs2h_apply_op(
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
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_mouse_over);
    case CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_mouse_leave);
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_mouse_repeat);
    case CS2VM_HOST_REQUEST_IF_SETONTIMER:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_timer);
    case CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_scroll_wheel);
    case CS2VM_HOST_REQUEST_IF_SETONKEY:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_key);
    case CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT:
        return exec_set_on_if_event(host, request->kind, &request->u.if_set_on_misc_transmit);
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
        return exec_set_on_cc_event(host, vm, request->kind, &request->u.cc_set_on_op);

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
            "GameInterfaceEditor_CS2HostExec: UNHANDLED request kind %d\n",
            (int)request->kind);
        return CS2VM_EXECNO_ERROR;
    }
}
