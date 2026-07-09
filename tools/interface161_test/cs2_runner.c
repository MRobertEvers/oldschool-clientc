#include "cs2_runner.h"

#include "enum_lookup.h"
#include "fixture.h"
#include "games/ie_enum_lookup.h"
#include "games/ie_struct_lookup.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_clientscript_convert.h"
#include "toriauxlib/core/toriauxlibcore_types.h"
#include "ui/rs_inv_container.h"
#include "ui/uitree.h"
#include "ui/uitree_build.h"
#include "ui/uitree_layout.h"
#include "vm/cs2_host_ui.h"
#include "vm/cs2_opcode.h"
#include "vm/cs2_script.h"
#include "vm/cs2_trigger_args.h"
#include "vm/cs2vm.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS2_RUNNER_SCRIPT_CACHE_MAX 64

static int const k_equipment_387_slot_files[] = { 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25 };
static int const k_equipment_387_slot_count =
    (int)(sizeof(k_equipment_387_slot_files) / sizeof(k_equipment_387_slot_files[0]));

struct Interface161Cs2ScriptEntry
{
    int script_id;
    struct ToriAuxLibCore_ClientScript* loaded;
};

struct Interface161Cs2RunnerState
{
    struct Interface161Cs2Context* ctx;
    struct RSCacheDat2Disk* disk;
    struct Interface161Cs2ScriptEntry scripts[CS2_RUNNER_SCRIPT_CACHE_MAX];
    int script_count;
    struct CS2Host host;
};

static struct Interface161Cs2RunnerState s_runner;
static int s_clientscript_decode_flags = CLIENTSCRIPT_DECODE_TRAILER_MODERN;

void
interface161_cs2_set_clientscript_decode_flags(int flags)
{
    s_clientscript_decode_flags = flags;
}

int
interface161_cs2_clientscript_decode_flags(void)
{
    return s_clientscript_decode_flags;
}

static int
interface161_cs2_resolve_decode_flags(void)
{
    if( s_runner.ctx )
        return s_runner.ctx->clientscript_decode_flags;
    return s_clientscript_decode_flags;
}

static void
interface161_cs2_run_component_hook(
    struct Interface161Cs2Context* ctx,
    RSCacheDat2A_Component* comp,
    ComponentScriptVar* hook,
    int hook_len);

static bool
interface161_cs2_resolve_obj_icon(
    void* ud,
    int obj_id,
    int* out_scene_id,
    int* out_atlas_index);

#define INTERFACE161_RUNTIME_INV_HOOK_MAX 32

struct Interface161RuntimeInvHook
{
    int target_component_id;
    int script_id;
    int argc;
    int argv[16];
    int trigger_count;
    int triggers[8];
};

static struct Interface161RuntimeInvHook s_runtime_inv_hooks[INTERFACE161_RUNTIME_INV_HOOK_MAX];
static int s_runtime_inv_hook_count;

static void
interface161_runtime_inv_hooks_reset(void)
{
    s_runtime_inv_hook_count = 0;
}

static void
interface161_runtime_store_inv_hook(
    int target_component_id,
    int script_id,
    int argc,
    int const* argv,
    int trigger_count,
    int const* triggers)
{
    if( target_component_id < 0 || script_id < 0 )
        return;

    for( int i = 0; i < s_runtime_inv_hook_count; i++ )
    {
        if( s_runtime_inv_hooks[i].target_component_id != target_component_id )
            continue;
        s_runtime_inv_hooks[i].script_id = script_id;
        s_runtime_inv_hooks[i].argc = argc > 16 ? 16 : argc;
        memcpy(
            s_runtime_inv_hooks[i].argv, argv, (size_t)s_runtime_inv_hooks[i].argc * sizeof(int));
        s_runtime_inv_hooks[i].trigger_count = trigger_count > 8 ? 8 : trigger_count;
        memcpy(
            s_runtime_inv_hooks[i].triggers,
            triggers,
            (size_t)s_runtime_inv_hooks[i].trigger_count * sizeof(int));
        return;
    }

    if( s_runtime_inv_hook_count >= INTERFACE161_RUNTIME_INV_HOOK_MAX )
        return;

    struct Interface161RuntimeInvHook* hook = &s_runtime_inv_hooks[s_runtime_inv_hook_count++];
    hook->target_component_id = target_component_id;
    hook->script_id = script_id;
    hook->argc = argc > 16 ? 16 : argc;
    memcpy(hook->argv, argv, (size_t)hook->argc * sizeof(int));
    hook->trigger_count = trigger_count > 8 ? 8 : trigger_count;
    memcpy(hook->triggers, triggers, (size_t)hook->trigger_count * sizeof(int));
}

static void
interface161_runtime_clear_inv_hook(int target_component_id)
{
    for( int i = 0; i < s_runtime_inv_hook_count; i++ )
    {
        if( s_runtime_inv_hooks[i].target_component_id != target_component_id )
            continue;
        for( int j = i + 1; j < s_runtime_inv_hook_count; j++ )
            s_runtime_inv_hooks[j - 1] = s_runtime_inv_hooks[j];
        s_runtime_inv_hook_count--;
        return;
    }
}

static void
interface161_parse_inv_transmit_trigger(
    struct CS2_InvokeCtx* ctx,
    int target_component_id)
{
    struct CS2TriggerArgs args;
    if( !cs2_trigger_args_parse(ctx, &args) || !args.ok )
        return;
    if( args.is_clear )
    {
        interface161_runtime_clear_inv_hook(target_component_id);
        return;
    }

    interface161_runtime_store_inv_hook(
        target_component_id,
        args.script_id,
        args.argc,
        args.argv,
        args.trigger_count,
        args.triggers);
}

static bool
interface161_opcode_is_seton(
    int opcode,
    bool* out_if_target)
{
    if( opcode >= CS2_OP_CC_SETONCLICK && opcode <= CS2_OP_CC_SETONCLANCHANNELTRANSMIT )
    {
        if( out_if_target )
            *out_if_target = false;
        return true;
    }
    if( opcode >= CS2_OP_IF_SETONCLICK && opcode <= CS2_OP_IF_SETONCLANCHANNELTRANSMIT )
    {
        if( out_if_target )
            *out_if_target = true;
        return true;
    }
    return false;
}

static void
interface161_host_invoke(
    void* ud,
    struct CS2_InvokeCtx* ctx)
{
    bool if_target = false;
    if( interface161_opcode_is_seton(ctx->opcode, &if_target) )
    {
        int target_component_id = ctx->active_component;
        if( if_target && !cs2vm_host_try_pop_int(ctx, &target_component_id) )
            return;

        if( ctx->opcode == CS2_OP_IF_SETONINVTRANSMIT || ctx->opcode == CS2_OP_CC_SETONINVTRANSMIT )
            interface161_parse_inv_transmit_trigger(ctx, target_component_id);
        else
        {
            struct CS2TriggerArgs args;
            (void)cs2_trigger_args_parse(ctx, &args);
        }
        return;
    }

    cs2_host_ui_invoke(ud, ctx);
}

static bool
interface161_runtime_hook_watches_container(
    struct Interface161RuntimeInvHook const* hook,
    int container_id)
{
    if( !hook || container_id < 0 )
        return false;
    if( hook->trigger_count <= 0 )
        return true;
    for( int i = 0; i < hook->trigger_count; i++ )
    {
        if( hook->triggers[i] == container_id )
            return true;
    }
    return false;
}

static void
interface161_cs2_run_runtime_inv_hook(
    struct Interface161Cs2Context* ctx,
    struct Interface161RuntimeInvHook const* hook,
    RSCacheDat2A_Component* comps,
    int comp_count)
{
    if( !ctx || !hook || hook->script_id < 0 )
        return;

    ComponentScriptVar hookv[17];
    memset(hookv, 0, sizeof(hookv));
    hookv[0].type = SCRIPT_VAR_INT;
    hookv[0].value.i = hook->script_id;
    int const hook_len = 1 + hook->argc;
    for( int i = 0; i < hook->argc; i++ )
    {
        hookv[i + 1].type = SCRIPT_VAR_INT;
        hookv[i + 1].value.i = hook->argv[i];
    }

    RSCacheDat2A_Component* comp = NULL;
    for( int i = 0; i < comp_count; i++ )
    {
        if( comps[i].id == hook->target_component_id )
        {
            comp = &comps[i];
            break;
        }
    }

    RSCacheDat2A_Component temp;
    if( !comp )
    {
        RSCacheDat2A_ComponentInit(&temp);
        temp.id = hook->target_component_id;
        comp = &temp;
    }

    interface161_cs2_run_component_hook(ctx, comp, hookv, hook_len);
}

static bool
interface161_parent_has_item_child(
    struct UITree const* tree,
    int32_t parent_idx,
    int obj_id)
{
    if( !tree || parent_idx < 0 || (uint32_t)parent_idx >= tree->component_count || obj_id <= 0 )
        return false;

    for( int32_t child = tree->components[parent_idx].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        struct StaticUIComponent const* node = &tree->components[child];
        if( node->item_id == obj_id )
            return true;
        if( node->type == UIELEM_CC_OBJ && node->u.cc_obj.obj_id == obj_id )
            return true;
    }
    return false;
}

static void
interface161_cs2_place_fixture_equipment_icons(
    struct Interface161Cs2Context* ctx,
    RSCacheDat2A_Component* comps,
    int comp_count,
    struct Interface161Fixture const* fixture)
{
    if( !ctx || !ctx->tree || !fixture || fixture->slot_count <= 0 )
        return;

    for( int si = 0; si < fixture->slot_count; si++ )
    {
        int const obj_id = fixture->slots[si].obj_id;
        if( obj_id <= 0 )
            continue;

        int target_id = -1;
        for( int i = 0; i < comp_count; i++ )
        {
            if( (comps[i].id & 0xFFFF) == fixture->slots[si].file_index )
            {
                target_id = comps[i].id;
                break;
            }
        }
        if( target_id < 0 )
            continue;

        int32_t parent_idx = uitree_find_by_component_id(ctx->tree, target_id);
        if( parent_idx < 0 )
            continue;
        if( interface161_parent_has_item_child(ctx->tree, parent_idx, obj_id) )
            continue;

        int const file_index = fixture->slots[si].file_index;
        int const sub_id = 0x8000 | (file_index & 0x7FFF);
        int32_t child_idx = uitree_cc_create(ctx->tree, parent_idx, target_id, 0, sub_id);
        if( child_idx < 0 )
            continue;

        int child_id = ctx->tree->components[child_idx].component_id;
        int scene_id = -1;
        int atlas_index = 0;
        (void)interface161_cs2_resolve_obj_icon(ctx, obj_id, &scene_id, &atlas_index);
        (void)uitree_apply_object(ctx->tree, child_id, obj_id, 1, scene_id, atlas_index);
    }
}

static int
equipment_slot_for_file(
    int iface_id,
    int file_index)
{
    if( iface_id != 387 )
        return -1;
    for( int i = 0; i < k_equipment_387_slot_count; i++ )
    {
        if( k_equipment_387_slot_files[i] == file_index )
            return i;
    }
    return -1;
}

static void
interface161_cs2_seed_empty_worn(struct Interface161Cs2Context* ctx)
{
    if( !ctx )
        return;

    struct RSInvContainer* container =
        rs_inv_container_get_or_create(&ctx->inv_data.store, RS_INV_CONTAINER_WORN, 14);
    if( !container )
        return;

    for( int slot = 0; slot < container->slot_count; slot++ )
        rs_inv_container_set_slot(container, slot, 0, 0, -1, 0);
}

static void
interface161_cs2_ensure_config_meta(struct Interface161Cs2Context* ctx)
{
    struct RSCacheDat2Disk_Archive* params_archive;
    struct RSCacheDat2Disk_Archive* enum_archive;
    struct RSCacheDat2Disk_Archive* struct_archive;

    if( !ctx || !ctx->cache || !ctx->dat2_bc || ctx->config_meta_loaded )
        return;

    params_archive = RSCacheDat2Disk_ArchiveNewLoad(
        ctx->cache, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Params);
    enum_archive = RSCacheDat2Disk_ArchiveNewLoad(
        ctx->cache, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Enum);
    struct_archive = RSCacheDat2Disk_ArchiveNewLoad(
        ctx->cache, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Struct);

    if( params_archive )
        dat2_buildcache_params_init_from_archive(ctx->dat2_bc, params_archive);
    if( enum_archive )
        dat2_buildcache_enums_init_from_archive(ctx->dat2_bc, enum_archive);
    if( struct_archive )
        dat2_buildcache_structs_init_from_archive(ctx->dat2_bc, struct_archive);

    RSCacheDat2Disk_ArchiveFree(params_archive);
    RSCacheDat2Disk_ArchiveFree(enum_archive);
    RSCacheDat2Disk_ArchiveFree(struct_archive);
    ctx->config_meta_loaded = true;
}

static int
interface161_cs2_enum_lookup(
    void* ud,
    int input_type,
    int output_type,
    int enum_id,
    int key)
{
    (void)ud;
    struct Interface161Cs2Context* ctx = s_runner.ctx;
    if( !ctx || !ctx->dat2_bc )
        return -1;
    interface161_cs2_ensure_config_meta(ctx);
    return ie_enum_lookup(ctx->dat2_bc, input_type, output_type, enum_id, key);
}

static char const*
interface161_cs2_enum_lookup_string(
    void* ud,
    int input_type,
    int output_type,
    int enum_id,
    int key)
{
    (void)ud;
    struct Interface161Cs2Context* ctx = s_runner.ctx;
    if( !ctx || !ctx->dat2_bc )
        return NULL;
    interface161_cs2_ensure_config_meta(ctx);
    return ie_enum_lookup_string(ctx->dat2_bc, input_type, output_type, enum_id, key);
}

static int
interface161_cs2_enum_output_count(
    void* ud,
    int enum_id)
{
    struct Interface161Cs2Context* ctx = ud;
    if( !ctx || !ctx->cache )
        return 0;
    return interface161_enum_output_count(ctx->cache, enum_id);
}

static bool
interface161_cs2_struct_param(
    void* ud,
    int struct_id,
    int param_id,
    bool* out_is_string,
    int* out_int,
    char const** out_str)
{
    struct Interface161Cs2Context* ctx = ud;
    if( !ctx || !ctx->dat2_bc )
        return false;
    interface161_cs2_ensure_config_meta(ctx);
    return ie_struct_param_lookup(
        ctx->dat2_bc, struct_id, param_id, out_is_string, out_int, out_str);
}

static void
interface161_cs2_seed_fixture(
    struct Interface161Cs2Context* ctx,
    int iface_id,
    struct Interface161Fixture const* fixture)
{
    if( !ctx || !fixture || fixture->slot_count <= 0 )
        return;

    int const container_id = iface_id == 387 ? RS_INV_CONTAINER_WORN : RS_INV_CONTAINER_BACKPACK;
    char const* source_name = container_id == RS_INV_CONTAINER_WORN ? UI_INV_SOURCE_NAME_WORN
                                                                    : UI_INV_SOURCE_NAME_BACKPACK;
    int const source_id = ui_inv_data_service_resolve_source(&ctx->inv_data, source_name);
    if( source_id < 0 )
        return;

    int backpack_slot = 0;
    for( int i = 0; i < fixture->slot_count; i++ )
    {
        int const obj_id = fixture->slots[i].obj_id;
        if( obj_id <= 0 )
            continue;

        int slot = equipment_slot_for_file(iface_id, fixture->slots[i].file_index);
        if( slot < 0 )
            slot = backpack_slot++;

        int scene_id = -1;
        int atlas_index = 0;
        if( ctx->scene && ctx->obj_icon_cache )
        {
            const int* icon =
                interface161_obj_icon_get(ctx->cache, ctx->scene, ctx->obj_icon_cache, obj_id);
            if( icon )
            {
                scene_id = obj_id;
                atlas_index = 0;
                (void)icon;
            }
        }

        struct UIInvSlotData data = {
            .obj_id = obj_id,
            .obj_count = 1,
            .scene_id = scene_id,
            .atlas_index = atlas_index,
        };
        (void)ui_inv_data_service_set_slot(&ctx->inv_data, source_id, slot, &data);

        struct RSInvContainer* container =
            rs_inv_container_get_or_create(&ctx->inv_data.store, container_id, 28);
        if( container )
        {
            rs_inv_container_set_slot(
                container, slot, obj_id, 1, scene_id >= 0 ? scene_id : -1, atlas_index);
        }
    }
}

static int
interface161_cs2_inv_get_obj(
    void* ud,
    int inv_id,
    int slot)
{
    struct Interface161Cs2Context* ctx = ud;
    struct RSInvContainer const* c =
        ctx ? rs_inv_container_find(&ctx->inv_data.store, inv_id) : NULL;
    if( !c || slot < 0 || slot >= c->slot_count )
        return 0;
    return c->obj_id[slot];
}

static int
interface161_cs2_inv_get_num(
    void* ud,
    int inv_id,
    int slot)
{
    struct Interface161Cs2Context* ctx = ud;
    struct RSInvContainer const* c =
        ctx ? rs_inv_container_find(&ctx->inv_data.store, inv_id) : NULL;
    if( !c || slot < 0 || slot >= c->slot_count )
        return 0;
    return c->obj_count[slot];
}

static int
interface161_cs2_inv_size(
    void* ud,
    int inv_id)
{
    struct Interface161Cs2Context* ctx = ud;
    struct RSInvContainer const* c =
        ctx ? rs_inv_container_find(&ctx->inv_data.store, inv_id) : NULL;
    return c ? c->slot_count : 0;
}

static bool
interface161_cs2_resolve_obj_icon(
    void* ud,
    int obj_id,
    int* out_scene_id,
    int* out_atlas_index)
{
    struct Interface161Cs2Context* ctx = ud;
    if( out_scene_id )
        *out_scene_id = -1;
    if( out_atlas_index )
        *out_atlas_index = 0;
    if( !ctx || obj_id <= 0 )
        return false;

    for( int src = 0; src < ctx->inv_data.source_count; src++ )
    {
        if( !ctx->inv_data.sources[src].used )
            continue;
        int const container_id = ctx->inv_data.sources[src].container_id;
        struct RSInvContainer const* container =
            rs_inv_container_find(&ctx->inv_data.store, container_id);
        if( !container )
            continue;
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

static struct RSCacheDat2Disk*
interface161_cs2_runner_disk(void)
{
    if( s_runner.disk )
        return s_runner.disk;
    if( s_runner.ctx && s_runner.ctx->cache )
        return s_runner.ctx->cache;
    return NULL;
}

static bool
interface161_cs2_cache_script(
    struct RSCacheDat2Disk* disk,
    int script_id)
{
    if( !disk || script_id < 0 )
        return false;

    for( int i = 0; i < s_runner.script_count; i++ )
    {
        if( s_runner.scripts[i].script_id == script_id )
            return true;
    }

    if( s_runner.script_count >= CS2_RUNNER_SCRIPT_CACHE_MAX )
    {
        fprintf(stderr, "interface161_cs2: script cache full, cannot load script %d\n", script_id);
        return false;
    }

    struct RSCacheDat2Disk_Archive* archive =
        RSCacheDat2Disk_ArchiveNewLoad(disk, RSCacheDat2Disk_Table_Clientscript, script_id);
    if( !archive )
    {
        fprintf(stderr, "interface161_cs2: script %d not found in cache\n", script_id);
        return false;
    }

    struct ToriAuxLibCore_ClientScript* loaded =
        ToriAuxLibCache_ClientScriptNewFromDat2Archive2(
            archive, script_id, interface161_cs2_resolve_decode_flags());
    if( !loaded || loaded->script.op_count <= 0 )
    {
        fprintf(stderr, "interface161_cs2: failed to decode script %d from cache\n", script_id);
        if( loaded )
            free(loaded);
        return false;
    }

    struct Interface161Cs2ScriptEntry* entry = &s_runner.scripts[s_runner.script_count++];
    entry->script_id = script_id;
    entry->loaded = loaded;
    return true;
}

static struct CS2_Script*
interface161_cs2_resolve_script(
    void* ud,
    int script_id)
{
    (void)ud;
    for( int i = 0; i < s_runner.script_count; i++ )
    {
        if( s_runner.scripts[i].script_id == script_id )
            return &s_runner.scripts[i].loaded->script;
    }

    struct RSCacheDat2Disk* disk = interface161_cs2_runner_disk();
    if( !disk || !interface161_cs2_cache_script(disk, script_id) )
        return NULL;

    for( int i = 0; i < s_runner.script_count; i++ )
    {
        if( s_runner.scripts[i].script_id == script_id )
            return &s_runner.scripts[i].loaded->script;
    }
    return NULL;
}

static void
interface161_cs2_run_component_hook(
    struct Interface161Cs2Context* ctx,
    RSCacheDat2A_Component* comp,
    ComponentScriptVar* hook,
    int hook_len)
{
    if( !ctx || !comp || !hook || hook_len <= 0 || hook[0].type != SCRIPT_VAR_INT )
        return;

    int script_id = hook[0].value.i;
    if( script_id < 0 )
        return;

    struct CS2_Script* script = interface161_cs2_resolve_script(ctx, script_id);
    if( !script )
    {
        fprintf(
            stderr,
            "interface161_cs2: hook script %d for comp 0x%x not resolved\n",
            script_id,
            comp->id);
        return;
    }

    int argv[16];
    int argc = hook_len - 1;
    if( argc > 16 )
        argc = 16;
    struct CS2_ScriptArgSubstitute const sub = { .component_id = comp->id };
    for( int i = 0; i < argc; i++ )
    {
        if( hook[i + 1].type == SCRIPT_VAR_INT )
            argv[i] = cs2_script_arg_substitute_int(hook[i + 1].value.i, &sub);
        else
            argv[i] = 0;
    }

    struct CS2_RunArgs args = {
        .int_argc = argc,
        .int_argv = argc > 0 ? argv : NULL,
        .string_argc = 0,
        .string_argv = NULL,
        .event_component_id = comp->id,
    };

    if( ctx->cs2vm )
    {
        int const rc = cs2vm_run(ctx->cs2vm, script, &s_runner.host, &args);
        if( rc != CS2VM_OK )
        {
            fprintf(
                stderr,
                "interface161_cs2: script %d for comp 0x%x failed rc=%d\n",
                script_id,
                comp->id,
                rc);
        }
    }
}

static bool
component_watches_container(
    RSCacheDat2A_Component* comp,
    int container_id)
{
    if( !comp || comp->onInvTransmitLen <= 0 )
        return false;
    if( comp->inventoryTriggersLen <= 0 )
        return true;
    for( int i = 0; i < comp->inventoryTriggersLen; i++ )
    {
        if( comp->inventoryTriggers[i] == container_id )
            return true;
    }
    return false;
}

struct Interface161BuildContext
{
    struct ToriAuxLibCore_Component** core_comps;
};

static struct ToriAuxLibCore_Component*
interface161_build_get_component(
    void* ud,
    int index)
{
    struct Interface161BuildContext* ctx = ud;
    if( !ctx || index < 0 )
        return NULL;
    return ctx->core_comps[index];
}

static int
interface161_build_get_parent_id(
    void* ud,
    int index)
{
    struct Interface161BuildContext* ctx = ud;
    if( !ctx || index < 0 || !ctx->core_comps[index] )
        return -1;
    return ctx->core_comps[index]->parent_id;
}

int
interface161_cs2_build_tree(
    struct UITree* tree,
    RSCacheDat2A_Component* comps,
    int comp_count)
{
    if( !tree || !comps || comp_count <= 0 )
        return -1;

    struct ToriAuxLibCore_Component** core_comps =
        calloc((size_t)comp_count, sizeof(*core_comps));
    if( !core_comps )
        return -1;

    for( int i = 0; i < comp_count; i++ )
    {
        if( comps[i].type < 0 )
            continue;
        core_comps[i] = ToriAuxLibCache_ComponentNewFromCacheDat2Component(&comps[i]);
        if( core_comps[i] )
            core_comps[i]->parent_id = comps[i].layer;
    }

    struct Interface161BuildContext ctx = { .core_comps = core_comps };
    struct UITreeBuildSource src = {
        .count = comp_count,
        .get_component = interface161_build_get_component,
        .get_parent_id = interface161_build_get_parent_id,
        .resolve_sprite = NULL,
        .ud = &ctx,
    };
    int const rc = uitree_build_from_source(tree, &src);

    for( int i = 0; i < comp_count; i++ )
        ToriAuxLibCore_ComponentFree(core_comps[i]);
    free(core_comps);
    return rc;
}

void
interface161_cs2_context_init(struct Interface161Cs2Context* ctx)
{
    if( !ctx )
        return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->dat2_bc = dat2_buildcache_new();
}

void
interface161_cs2_context_free(struct Interface161Cs2Context* ctx)
{
    if( !ctx )
        return;

    if( ctx->dat2_bc )
        dat2_buildcache_free(ctx->dat2_bc);
    ctx->dat2_bc = NULL;
    if( ctx->tree )
        uitree_free(ctx->tree);
    if( ctx->cs2vm )
        cs2vm_free(ctx->cs2vm);
    for( int i = 0; i < s_runner.script_count; i++ )
    {
        if( s_runner.scripts[i].loaded )
        {
            cs2_script_free(&s_runner.scripts[i].loaded->script);
            free(s_runner.scripts[i].loaded);
        }
    }
    memset(ctx, 0, sizeof(*ctx));
    memset(&s_runner, 0, sizeof(s_runner));
}

static int
interface161_cs2_init_host_shell(
    struct Interface161Cs2Context* ctx,
    RSCacheDat2A_Component* comps,
    int comp_count,
    int iface_id,
    struct Interface161Fixture const* fixture)
{
    if( !ctx || !ctx->cache || !comps || comp_count <= 0 )
        return -1;

    memset(&s_runner, 0, sizeof(s_runner));
    interface161_runtime_inv_hooks_reset();
    s_runner.ctx = ctx;
    s_runner.disk = ctx->cache;
    ctx->tree = uitree_new(64);
    ctx->cs2vm = cs2vm_new();
    if( !ctx->tree || !ctx->cs2vm )
        return -1;

    ui_inv_data_service_init(&ctx->inv_data);
    char const* source_name =
        iface_id == 387 ? UI_INV_SOURCE_NAME_WORN : UI_INV_SOURCE_NAME_BACKPACK;
    (void)ui_inv_data_service_resolve_source(&ctx->inv_data, source_name);

    if( fixture && fixture->slot_count > 0 )
        interface161_cs2_seed_fixture(ctx, iface_id, fixture);
    else if( iface_id == 387 )
        interface161_cs2_seed_empty_worn(ctx);

    if( interface161_cs2_build_tree(ctx->tree, comps, comp_count) != 0 )
        return -1;

    uitree_layout_resolve(ctx->tree, 0, 0, ctx->root_w, ctx->root_h);

    struct CS2HostUIInitArgs args = {
        .core = NULL,
        .cache = NULL,
        .vm = NULL,
        .tree = ctx->tree,
        .resolve_obj_icon = interface161_cs2_resolve_obj_icon,
        .resolve_obj_icon_ud = ctx,
        .inv_get_obj = interface161_cs2_inv_get_obj,
        .inv_get_num = interface161_cs2_inv_get_num,
        .inv_size = interface161_cs2_inv_size,
        .inv_ud = ctx,
        .enum_output_count = interface161_cs2_enum_output_count,
        .enum_output_count_ud = ctx,
        .struct_param = interface161_cs2_struct_param,
        .struct_param_ud = ctx,
        .viewport_w = ctx->root_w,
        .viewport_h = ctx->root_h,
        .client_type = 80,
        .window_mode = 1,
    };
    cs2_host_ui_init(&s_runner.host, &args);
    s_runner.host.resolve_script = interface161_cs2_resolve_script;
    s_runner.host.enum_lookup = interface161_cs2_enum_lookup;
    s_runner.host.enum_lookup_string = interface161_cs2_enum_lookup_string;
    s_runner.host.invoke = interface161_host_invoke;
    ctx->cs2host = &s_runner.host;
    return 0;
}

int
interface161_cs2_prepare_exec_shell(
    struct Interface161Cs2Context* ctx,
    RSCacheDat2A_Component* comps,
    int comp_count,
    int iface_id,
    int root_w,
    int root_h)
{
    if( !ctx )
        return -1;
    ctx->root_w = root_w;
    ctx->root_h = root_h;
    return interface161_cs2_init_host_shell(ctx, comps, comp_count, iface_id, NULL);
}

static void
interface161_cs2_seed_gameframe_s35_children(struct UITree* tree)
{
    if( !tree )
        return;

    int const s35_uid = (165 << 16) | 35;
    int32_t parent_idx = uitree_find_by_component_id(tree, s35_uid);
    if( parent_idx < 0 )
        return;

    int dynamic_count = 0;
    for( int32_t child = tree->components[parent_idx].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        if( tree->components[child].dynamic )
            dynamic_count++;
    }
    if( dynamic_count >= 3 )
        return;

    for( int sub = dynamic_count; sub < 3; sub++ )
    {
        int32_t child_idx = uitree_cc_create(tree, parent_idx, s35_uid, 3, sub);
        if( child_idx >= 0 )
        {
            tree->components[child_idx].position.width = 1;
            tree->components[child_idx].position.height = 1;
            tree->components[child_idx].position.layout_resolved = 0;
        }
    }
}

int
interface161_cs2_run_interface(
    struct Interface161Cs2Context* ctx,
    RSCacheDat2A_Component* comps,
    int comp_count,
    int const* lay_x,
    int const* lay_y,
    int const* lay_w,
    int const* lay_h,
    int iface_id,
    struct Interface161Fixture const* fixture)
{
    (void)lay_x;
    (void)lay_y;
    (void)lay_w;
    (void)lay_h;
    if( interface161_cs2_init_host_shell(ctx, comps, comp_count, iface_id, fixture) != 0 )
        return -1;

    uitree_layout_resolve(ctx->tree, 0, 0, ctx->root_w, ctx->root_h);

    for( int i = 0; i < comp_count; i++ )
    {
        if( comps[i].type < 0 )
            continue;
        if( comps[i].onLoadLen <= 0 )
            continue;
        interface161_cs2_run_component_hook(ctx, &comps[i], comps[i].onLoad, comps[i].onLoadLen);
    }
    uitree_layout_resolve(ctx->tree, 0, 0, ctx->root_w, ctx->root_h);

    for( int i = 0; i < comp_count; i++ )
    {
        if( comps[i].type < 0 )
            continue;
        if( comps[i].onLoadLen <= 0 )
            continue;
        interface161_cs2_run_component_hook(ctx, &comps[i], comps[i].onLoad, comps[i].onLoadLen);
    }
    uitree_layout_resolve(ctx->tree, 0, 0, ctx->root_w, ctx->root_h);

    for( int h = 0; h < s_runtime_inv_hook_count; h++ )
    {
        if( s_runtime_inv_hooks[h].trigger_count <= 0 )
            continue;
        interface161_cs2_run_runtime_inv_hook(ctx, &s_runtime_inv_hooks[h], comps, comp_count);
    }

    for( int i = 0; i < comp_count; i++ )
    {
        if( comps[i].type < 0 )
            continue;
        if( comps[i].onInvTransmitLen <= 0 )
            continue;
        if( comps[i].inventoryTriggersLen <= 0 )
            continue;
        interface161_cs2_run_component_hook(
            ctx, &comps[i], comps[i].onInvTransmit, comps[i].onInvTransmitLen);
    }

    uitree_layout_resolve(ctx->tree, 0, 0, ctx->root_w, ctx->root_h);

    if( iface_id == 165 )
        interface161_cs2_seed_gameframe_s35_children(ctx->tree);
    uitree_layout_resolve(ctx->tree, 0, 0, ctx->root_w, ctx->root_h);

    int dynamic_count = 0;
    for( uint32_t i = 0; i < ctx->tree->component_count; i++ )
    {
        if( ctx->tree->components[i].dynamic )
            dynamic_count++;
    }
    int onload_count = 0;
    for( int i = 0; i < comp_count; i++ )
    {
        if( comps[i].type >= 0 && comps[i].onLoadLen > 0 )
            onload_count++;
    }
    fprintf(
        stderr,
        "interface161_cs2: iface %d onload_hooks=%d dynamic_children=%d tree_components=%u\n",
        iface_id,
        onload_count,
        dynamic_count,
        ctx->tree->component_count);

    return 0;
}
