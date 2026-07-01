#include "cs2_host_ui.h"

#include "cs2_opcode.h"
#include "cs2vm.h"
#include "osrs/varp_varbit_manager.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/vm/toriauxlibvm.h"
#include "ui/uitree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS2_HOST_UI_VARC_INT_MAX 256
#define CS2_HOST_UI_VARC_STRING_MAX 64
#define CS2_HOST_UI_VARC_STRING_LEN 128

struct CS2HostUIState
{
    struct ToriAuxLibCore* core;
    struct ToriAuxLibCache* cache;
    struct ToriAuxLibVM* vm;
    struct UITree* tree;
    CS2HostUIVarpChangeFn on_varp_change;
    void* on_varp_change_ud;
    int varc_int[CS2_HOST_UI_VARC_INT_MAX];
    char varc_string[CS2_HOST_UI_VARC_STRING_MAX][CS2_HOST_UI_VARC_STRING_LEN];
};

static struct CS2HostUIState s_cs2_host_ui_state;

static int32_t
cs2_host_ui_resolve_target(
    struct CS2HostUIState const* st,
    int component_id,
    int active_component)
{
    if( !st || !st->tree )
        return -1;
    if( component_id >= 0 )
        return uitree_find_by_component_id(st->tree, component_id);
    if( active_component >= 0 )
        return uitree_find_by_component_id(st->tree, active_component);
    return -1;
}

static int
cs2_host_ui_get_varp(
    void* ud,
    int id)
{
    struct CS2HostUIState* st = ud;
    return st && st->vm ? ToriAuxLibVM_GetVarp(st->vm, id) : 0;
}

static int
cs2_host_ui_get_varbit(
    void* ud,
    int id)
{
    struct CS2HostUIState* st = ud;
    return st && st->vm ? ToriAuxLibVM_GetVarbit(st->vm, id) : 0;
}

static int
cs2_host_ui_get_varc_int(
    void* ud,
    int id)
{
    struct CS2HostUIState* st = ud;
    if( !st || id < 0 || id >= CS2_HOST_UI_VARC_INT_MAX )
        return 0;
    return st->varc_int[id];
}

static const char*
cs2_host_ui_get_varc_string(
    void* ud,
    int id)
{
    struct CS2HostUIState* st = ud;
    if( !st || id < 0 || id >= CS2_HOST_UI_VARC_STRING_MAX )
        return "";
    return st->varc_string[id];
}

static void
cs2_host_ui_notify_varp_change(
    struct CS2HostUIState* st,
    int id)
{
    if( st && st->on_varp_change )
        st->on_varp_change(st->on_varp_change_ud, id);
}

static void
cs2_host_ui_set_varp(
    void* ud,
    int id,
    int value)
{
    struct CS2HostUIState* st = ud;
    if( st && st->vm )
    {
        ToriAuxLibVM_SetVarpOptimistic(st->vm, id, value);
        cs2_host_ui_notify_varp_change(st, id);
    }
}

static void
cs2_host_ui_set_varbit(
    void* ud,
    int id,
    int value)
{
    struct CS2HostUIState* st = ud;
    if( !st || !st->vm )
        return;

    struct VarPVarBitManager* mgr = ToriAuxLibVM_VarPVarBit(st->vm);
    if( !mgr || id < 0 || id >= mgr->varbit_count )
        return;

    struct VarBitType const* vb = &mgr->varbit_types[id];
    if( vb->basevar < 0 || vb->basevar >= mgr->varp_count )
        return;

    int bit_count = vb->endbit - vb->startbit;
    if( bit_count <= 0 || bit_count >= VARP_VARBIT_READBIT_MAX )
        return;

    int mask = mgr->readbit[bit_count];
    int base_val = varp_varbit_get_varp(mgr, vb->basevar);
    int cleared = base_val & ~(mask << vb->startbit);
    int updated = cleared | ((value & mask) << vb->startbit);
    varp_varbit_set_varp_optimistic(mgr, vb->basevar, updated);
    cs2_host_ui_notify_varp_change(st, vb->basevar);
}

void
cs2_host_ui_set_varc_int(
    struct CS2HostUIState* st,
    int id,
    int value)
{
    if( !st || id < 0 || id >= CS2_HOST_UI_VARC_INT_MAX )
        return;
    st->varc_int[id] = value;
}

void
cs2_host_ui_set_varc_string(
    struct CS2HostUIState* st,
    int id,
    char const* value)
{
    if( !st || id < 0 || id >= CS2_HOST_UI_VARC_STRING_MAX )
        return;
    strncpy(st->varc_string[id], value ? value : "", CS2_HOST_UI_VARC_STRING_LEN - 1);
    st->varc_string[id][CS2_HOST_UI_VARC_STRING_LEN - 1] = '\0';
}

static void
cs2_host_ui_set_varc_int_cb(
    void* ud,
    int id,
    int value)
{
    cs2_host_ui_set_varc_int((struct CS2HostUIState*)ud, id, value);
}

static void
cs2_host_ui_set_varc_string_cb(
    void* ud,
    int id,
    char const* value)
{
    cs2_host_ui_set_varc_string((struct CS2HostUIState*)ud, id, value);
}

static struct CS2_Script*
cs2_host_ui_resolve_script(
    void* ud,
    int script_id)
{
    struct CS2HostUIState* st = ud;
    if( !st )
        return NULL;
    struct ToriAuxLibCore_ClientScript* script = NULL;
    if( st->core )
        script = ToriAuxLibCore_ClientScriptGet(st->core, script_id);
    if( !script && st->cache )
        script = ToriAuxLibCache_ClientScriptResolve(st->cache, script_id);
    return script ? &script->script : NULL;
}

static void
cs2_host_ui_apply_hide(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    int hide)
{
    if( !st || !st->tree )
        return;
    int32_t idx = cs2_host_ui_resolve_target(st, component_id, active_component);
    if( idx < 0 )
        return;
    st->tree->components[idx].behavior.hide = hide ? 1 : 0;
    uitree_mark_node_dirty(st->tree, idx);
}

static void
cs2_host_ui_apply_text(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    char const* text)
{
    if( !st || !st->tree )
        return;
    (void)uitree_apply_text(st->tree, component_id >= 0 ? component_id : active_component, text);
}

static void
cs2_host_ui_apply_graphic(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    int graphic_id)
{
    if( !st || !st->tree )
        return;
    (void)uitree_apply_graphic(
        st->tree, component_id >= 0 ? component_id : active_component, graphic_id);
}

void
cs2_host_ui_invoke(
    void* ud,
    struct CS2_InvokeCtx* ctx)
{
    struct CS2HostUIState* st = ud;
    if( !ctx || !st )
        return;

    switch( ctx->opcode )
    {
    case CS2_OP_CC_CREATE:
        (void)cs2vm_host_pop_int(ctx);
        (void)cs2vm_host_pop_int(ctx);
        (void)cs2vm_host_pop_int(ctx);
        break;
    case CS2_OP_CC_DELETEALL:
        break;
    case CS2_OP_CC_FIND:
    {
        int sub = cs2vm_host_pop_int(ctx);
        (void)sub;
        int parent = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, parent);
        cs2vm_host_push_int(ctx, parent >= 0);
        break;
    }
    case CS2_OP_IF_FIND:
    {
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2vm_host_push_int(ctx, component >= 0);
        break;
    }
    case CS2_OP_CC_SETHIDE:
    {
        int hide = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_hide(st, -1, ctx->active_component, hide);
        break;
    }
    case CS2_OP_CC_SETTEXT:
    {
        char* text = cs2vm_host_pop_string(ctx);
        cs2_host_ui_apply_text(st, -1, ctx->active_component, text ? text : "");
        break;
    }
    case CS2_OP_CC_SETGRAPHIC:
    {
        int graphic = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_graphic(st, -1, ctx->active_component, graphic);
        break;
    }
    case CS2_OP_IF_SETHIDE:
    {
        int hide = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2_host_ui_apply_hide(st, component, ctx->active_component, hide);
        break;
    }
    case CS2_OP_IF_SETTEXT:
    {
        char* text = cs2vm_host_pop_string(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2_host_ui_apply_text(st, component, ctx->active_component, text ? text : "");
        break;
    }
    case CS2_OP_IF_SETGRAPHIC:
    {
        int graphic = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2_host_ui_apply_graphic(st, component, ctx->active_component, graphic);
        break;
    }
    case CS2_OP_IF_SETCOLOUR:
    {
        int colour = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        if( st->tree )
            (void)uitree_apply_colour(st->tree, component, colour);
        break;
    }
    case CS2_OP_IF_SETPOSITION:
    {
        int y = cs2vm_host_pop_int(ctx);
        int x = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        if( st->tree )
            (void)uitree_apply_position(st->tree, component, x, y);
        break;
    }
    case CS2_OP_IF_SETSIZE:
    {
        int height = cs2vm_host_pop_int(ctx);
        int width = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        if( st->tree )
            (void)uitree_apply_size(st->tree, component, width, height);
        break;
    }
    case CS2_OP_IF_SETSCROLLSIZE:
    {
        int scroll_height = cs2vm_host_pop_int(ctx);
        int scroll_width = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        if( st->tree )
            (void)uitree_apply_scroll_size(st->tree, component, scroll_width, scroll_height);
        break;
    }
    case CS2_OP_IF_SETSCROLLPOS:
        (void)cs2vm_host_pop_int(ctx);
        (void)cs2vm_host_pop_int(ctx);
        (void)cs2vm_host_pop_int(ctx);
        break;
    default:
        break;
    }
}

void
cs2_host_ui_init(
    struct CS2Host* host,
    struct CS2HostUIInitArgs const* args)
{
    if( !host )
        return;

    memset(&s_cs2_host_ui_state, 0, sizeof(s_cs2_host_ui_state));
    if( args )
    {
        s_cs2_host_ui_state.core = args->core;
        s_cs2_host_ui_state.cache = args->cache;
        s_cs2_host_ui_state.vm = args->vm;
        s_cs2_host_ui_state.tree = args->tree;
        s_cs2_host_ui_state.on_varp_change = args->on_varp_change;
        s_cs2_host_ui_state.on_varp_change_ud = args->on_varp_change_ud;
    }

    memset(host, 0, sizeof(*host));
    host->ud = &s_cs2_host_ui_state;
    host->get_varp = cs2_host_ui_get_varp;
    host->get_varbit = cs2_host_ui_get_varbit;
    host->get_varc_int = cs2_host_ui_get_varc_int;
    host->get_varc_string = cs2_host_ui_get_varc_string;
    host->set_varp = cs2_host_ui_set_varp;
    host->set_varbit = cs2_host_ui_set_varbit;
    host->set_varc_int = cs2_host_ui_set_varc_int_cb;
    host->set_varc_string = cs2_host_ui_set_varc_string_cb;
    host->resolve_script = cs2_host_ui_resolve_script;
    host->invoke = cs2_host_ui_invoke;
}
