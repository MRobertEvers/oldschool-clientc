/*
 * CS1VM game host. Mirrors rs_cs2_host: a flat dispatch over the request kinds,
 * with the pending/awaited pair implementing the one-opcode-one-yield contract
 * for the only reads that can miss — the inventory opcodes, which name a
 * component that may live in an interface that is not mounted yet.
 */

#include "rs_cs1_host.h"

#include "engine/cache_provider.h"
#include "inv/inv_manager.h"
#include "ui/uitree.h"
#include "varp/varp_manager.h"

#include <assert.h>
#include <string.h>

void
RS_CS1Host_Init(
    struct RS_CS1Host* host,
    struct UITree* tree,
    struct CacheProvider* provider,
    struct InvManager* invs,
    struct VarPManager* varps,
    struct RS_PlayerStats* stats)
{
    assert(host);

    memset(host, 0, sizeof(*host));
    host->tree = tree;
    host->provider = provider;
    host->invs = invs;
    host->varps = varps;
    host->stats = stats;

    CS1VM_Init(&host->vm);
    CS1VM_BindHost(&host->vm, host, RS_CS1Host_Exec);
}

static int
rs_cs1_yield(
    struct RS_CS1Host* host,
    struct CS1VM_HostRequest const* request)
{
    assert(host);
    assert(request);

    host->pending = *request;
    host->has_pending = true;
    return CS1VM_EXECNO_YIELD;
}

/**
 * One opcode, one yield. `rs_cs1_yield_load` parks a request for the task layer
 * and records what it is waiting for; `rs_cs1_await_spent` tells the handler on
 * the retry that this exact wait already happened, so a resource that is still
 * missing is genuinely absent and the handler must complete with a default
 * rather than yield again (which the VM's yield-halt guard treats as a bug).
 */
static bool
rs_cs1_await_spent(
    struct RS_CS1Host* host,
    enum CS1VM_HostRequestKind kind,
    int id)
{
    assert(host);
    return host->has_awaited && host->awaited_kind == kind && host->awaited_id == id;
}

static int
rs_cs1_yield_load(
    struct RS_CS1Host* host,
    struct CS1VM_HostRequest const* request,
    int id)
{
    assert(host);
    assert(request);

    host->awaited_kind = request->kind;
    host->awaited_id = id;
    host->has_awaited = true;
    return rs_cs1_yield(host, request);
}

static struct UITreeComponent const*
rs_cs1_node(
    struct RS_CS1Host* host,
    int component_id)
{
    assert(host);

    if( !host->tree )
        return NULL;

    int32_t idx = UITree_FindByComponentId(host->tree, component_id);
    if( idx < 0 )
        return NULL;
    return &host->tree->components[idx];
}

/** The inv container a component draws from, or -1 if it is not an inv widget. */
static int
rs_cs1_component_inv_source(struct UITreeComponent const* component)
{
    assert(component);

    switch( component->type )
    {
    case UIELEM_RS_INV:
        return component->u.rs_inv.inv_source_id;
    case UIELEM_RS_INV_TEXT:
        return component->u.rs_inv_text.inv_source_id;
    case UIELEM_BUILTIN_SIDEBAR:
        return component->u.sidebar.inv_source_id;
    default:
        return -1;
    }
}

/**
 * Resolve an inventory opcode's {component_id, obj_id}. Yields once if the
 * named component is not in the tree: the task layer loads its pack, and on the
 * retry the component either exists or genuinely does not, so the read
 * completes with a count of 0.
 */
static int
rs_cs1_exec_inv(
    struct RS_CS1Host* host,
    struct CS1VM_HostRequest* request)
{
    assert(host);
    assert(request);

    int component_id = request->u.inv.component_id;
    int obj_id = request->u.inv.obj_id;

    request->out_value = 0;
    if( component_id < 0 || obj_id < 0 )
        return CS1VM_EXECNO_OK;

    struct UITreeComponent const* component = rs_cs1_node(host, component_id);
    if( !component )
    {
        if( rs_cs1_await_spent(host, request->kind, component_id) )
            return CS1VM_EXECNO_OK;
        return rs_cs1_yield_load(host, request, component_id);
    }

    int source_id = rs_cs1_component_inv_source(component);
    if( source_id < 0 || !host->invs )
        return CS1VM_EXECNO_OK;

    int container_id = InvManager_ContainerForSource(host->invs, source_id);
    if( container_id < 0 )
        return CS1VM_EXECNO_OK;

    /* Containers store real obj ids (0 = empty slot), so the script's obj id is
     * compared as-is — no +1 offset like the reference client's linkObjType. */
    int total = InvManager_Total(host->invs, container_id, obj_id);

    if( request->kind == CS1VM_HOST_REQUEST_INV_CONTAINS )
        request->out_value = total > 0 ? CS1VM_VALUE_INFINITY : 0;
    else
        request->out_value = total;

    return CS1VM_EXECNO_OK;
}

static int
rs_cs1_exec_stat(
    struct RS_CS1Host* host,
    struct CS1VM_HostRequest* request)
{
    assert(host);
    assert(request);

    int skill = request->u.stat.skill;
    bool in_range = skill >= 0 && skill < RS_PLAYER_STATS_SKILL_COUNT;

    /* Level defaults to 1 (a fresh account) so level-gated interfaces render
     * sensibly before any stat sync exists; xp defaults to 0. */
    switch( request->kind )
    {
    case CS1VM_HOST_REQUEST_STAT_LEVEL:
        request->out_value = (host->stats && in_range) ? host->stats->current_level[skill] : 1;
        break;
    case CS1VM_HOST_REQUEST_STAT_BASE_LEVEL:
        request->out_value = (host->stats && in_range) ? host->stats->base_level[skill] : 1;
        break;
    case CS1VM_HOST_REQUEST_STAT_XP:
        request->out_value = (host->stats && in_range) ? host->stats->xp[skill] : 0;
        break;
    case CS1VM_HOST_REQUEST_STAT_XP_REMAINING:
        request->out_value = host->stats ? RS_PlayerStats_XpRemaining(host->stats, skill) : 0;
        break;
    default:
        request->out_value = 0;
        break;
    }

    return CS1VM_EXECNO_OK;
}

static int
rs_cs1_host_exec_dispatch(
    struct RS_CS1Host* host,
    struct CS1VM_HostRequest* request)
{
    assert(host);
    assert(request);

    switch( request->kind )
    {
    case CS1VM_HOST_REQUEST_STAT_LEVEL:
    case CS1VM_HOST_REQUEST_STAT_BASE_LEVEL:
    case CS1VM_HOST_REQUEST_STAT_XP:
    case CS1VM_HOST_REQUEST_STAT_XP_REMAINING:
        return rs_cs1_exec_stat(host, request);

    case CS1VM_HOST_REQUEST_INV_COUNT:
    case CS1VM_HOST_REQUEST_INV_CONTAINS:
        return rs_cs1_exec_inv(host, request);

    case CS1VM_HOST_REQUEST_VARP:
        request->out_value =
            host->varps ? VarPManager_GetVarp(host->varps, request->u.varp.varp_id) : 0;
        return CS1VM_EXECNO_OK;

    case CS1VM_HOST_REQUEST_VARBIT:
        request->out_value =
            host->varps ? VarPManager_GetVarbit(host->varps, request->u.varbit.varbit_id) : 0;
        return CS1VM_EXECNO_OK;

    case CS1VM_HOST_REQUEST_COMBAT_LEVEL:
        request->out_value = host->stats ? host->stats->combat_level : 3;
        return CS1VM_EXECNO_OK;

    case CS1VM_HOST_REQUEST_TOTAL_LEVEL:
        request->out_value = host->stats ? RS_PlayerStats_TotalLevel(host->stats) : 0;
        return CS1VM_EXECNO_OK;

    case CS1VM_HOST_REQUEST_RUNENERGY:
        request->out_value = host->stats ? host->stats->run_energy : 100;
        return CS1VM_EXECNO_OK;

    case CS1VM_HOST_REQUEST_RUNWEIGHT:
        request->out_value = host->stats ? host->stats->run_weight : 0;
        return CS1VM_EXECNO_OK;

    case CS1VM_HOST_REQUEST_COORD_X:
        request->out_value = host->stats ? host->stats->coord_x : 0;
        return CS1VM_EXECNO_OK;

    case CS1VM_HOST_REQUEST_COORD_Z:
        request->out_value = host->stats ? host->stats->coord_z : 0;
        return CS1VM_EXECNO_OK;
    }

    request->out_value = 0;
    return CS1VM_EXECNO_OK;
}

int
RS_CS1Host_Exec(
    struct CS1VM* vm,
    struct CS1VM_HostRequest* request)
{
    struct RS_CS1Host* host = (struct RS_CS1Host*)CS1VM_USER(vm);

    assert(host);
    assert(request);

    int rc = rs_cs1_host_exec_dispatch(host, request);

    /* The await record spans only the yield -> load -> retry window. */
    if( rc != CS1VM_EXECNO_YIELD )
        host->has_awaited = false;

    return rc;
}

/** Borrowed view of a component's CS1 arrays, as stored by UITree_SetBehavior. */
static void
rs_cs1_component_scripts(
    struct UITreeComponent const* component,
    struct CS1VM_ComponentScripts* out)
{
    assert(component);
    assert(out);

    memset(out, 0, sizeof(*out));
    out->scripts_count = component->behavior.scripts_count;
    out->scripts = component->behavior.scripts;
    out->scripts_lengths = component->behavior.scripts_lengths;
    out->comparator_count = component->behavior.comparator_count;
    out->comparator = component->behavior.script_comparator;
    out->operand = component->behavior.script_operand;
}

enum CS1VM_EvalStatus
RS_CS1Host_ComponentActive(
    struct RS_CS1Host* host,
    struct UITreeComponent const* component,
    bool* out_active)
{
    struct CS1VM_ComponentScripts set;

    assert(host);
    assert(out_active);

    *out_active = false;
    if( !component || component->behavior.scripts_count <= 0 )
        return CS1VM_EVAL_DONE;

    rs_cs1_component_scripts(component, &set);
    return CS1VM_EvalActive(&host->vm, &set, out_active);
}

enum CS1VM_EvalStatus
RS_CS1Host_ComponentValue(
    struct RS_CS1Host* host,
    struct UITreeComponent const* component,
    int script_index,
    int* out_value)
{
    struct CS1VM_ComponentScripts set;

    assert(host);
    assert(out_value);

    *out_value = 0;
    if( !component || component->behavior.scripts_count <= 0 )
        return CS1VM_EVAL_DONE;

    rs_cs1_component_scripts(component, &set);

    int value = 0;
    enum CS1VM_EvalStatus status = CS1VM_EvalValue(&host->vm, &set, script_index, &value);
    if( status == CS1VM_EVAL_DONE )
        *out_value = value;
    return status;
}
