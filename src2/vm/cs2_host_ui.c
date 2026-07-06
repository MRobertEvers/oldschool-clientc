#include "cs2_host_ui.h"

#include "cs2_opcode.h"
#include "cs2_opcode_meta.h"
#include "cs2vm.h"
#include "games/ie_enum_lookup.h"
#include "games/ie_param_lookup.h"
#include "games/ie_struct_lookup.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/varp_varbit_manager.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/vm/toriauxlibvm.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_scene.h"
#include "ui/uitree.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CS2_HOST_UI_VARC_INT_MAX 256
#define CS2_HOST_UI_VARC_STRING_MAX 64
#define CS2_HOST_UI_VARC_STRING_LEN 128

#define CS2_OP_CC_CLEAROPSUBMENU 1310
#define CS2_OP_CC_SETOPSUBMENU 1311
#define CS2_OP_IF_CLEAROPSUBMENU 2310
#define CS2_OP_IF_SETOPSUBMENU 2311

struct CS2HostUIState
{
    struct ToriAuxLibCore* core;
    struct ToriAuxLibCache* cache;
    struct RSCacheDat2Disk* dat2_disk;
    struct ToriAuxLibVM* vm;
    struct UITree* tree;
    CS2HostUIVarpChangeFn on_varp_change;
    void* on_varp_change_ud;
    CS2HostUIResolveObjIconFn resolve_obj_icon;
    void* resolve_obj_icon_ud;
    CS2HostUIInvGetObjFn inv_get_obj;
    CS2HostUIInvGetNumFn inv_get_num;
    CS2HostUIInvSizeFn inv_size;
    void* inv_ud;
    CS2HostUIEnumOutputCountFn enum_output_count;
    void* enum_output_count_ud;
    CS2HostUIStructParamFn struct_param;
    void* struct_param_ud;
    CS2HostUICcCreateFn cc_create_notify;
    void* cc_create_notify_ud;
    struct ToriDraw_Scene* scene;
    int viewport_w;
    int viewport_h;
    int client_type;
    int32_t active_node_index;
    int clock_ticks;
    int varc_int[CS2_HOST_UI_VARC_INT_MAX];
    char varc_string[CS2_HOST_UI_VARC_STRING_MAX][CS2_HOST_UI_VARC_STRING_LEN];
};

static struct CS2HostUIState s_cs2_host_ui_state;

static struct RSCacheDat2Disk*
cs2_host_ui_dat2_disk(struct CS2HostUIState const* st)
{
    if( !st )
        return NULL;
    if( st->dat2_disk )
        return st->dat2_disk;
    if( st->cache )
        return ToriAuxLibCache_Dat2Disk(st->cache);
    return NULL;
}

static struct RSCacheDat2A_ConfigObject*
cs2_host_ui_load_object_config(
    struct RSCacheDat2Disk* cache,
    int obj_id)
{
    if( !cache || obj_id <= 0 )
        return NULL;

    struct RSCacheDat2Disk_Archive* arch = RSCacheDat2Disk_ArchiveNewLoad(
        cache, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Object);
    if( !arch )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadata(cache, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl )
    {
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    struct RSCacheDat2Disk_ReferenceTable* table = cache->tables[RSCacheDat2Disk_Table_Configs];
    struct RSCacheDat2Disk_ArchiveReference* ref = NULL;
    if( table && table->archives )
        ref = &table->archives[RSCacheDat2A_ConfigKind_Object];

    struct RSCacheDat2A_ConfigObject* out = NULL;
    for( int i = 0; i < fl->file_count; i++ )
    {
        int id = (ref && i < ref->children.count) ? ref->children.files[i].id : i;
        if( id != obj_id )
            continue;

        out = calloc(1, sizeof(struct RSCacheDat2A_ConfigObject));
        if( !out )
            break;

        RSCacheDat2A_ConfigObjectDecodeInplace(out, fl->files[i], fl->file_sizes[i]);
        out->_id = obj_id;
        break;
    }

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    return out;
}

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
cs2_host_ui_get_layer(
    struct CS2HostUIState const* st,
    int component_id)
{
    if( !st || !st->tree || component_id < 0 )
        return -1;

    int32_t idx = uitree_find_by_component_id(st->tree, component_id);
    if( idx < 0 )
        return -1;

    int32_t parent_idx = st->tree->components[idx].parent;
    if( parent_idx < 0 )
        return -1;

    return st->tree->components[parent_idx].component_id;
}

static int
cs2_host_ui_get_pos_x(
    struct CS2HostUIState const* st,
    int component_id)
{
    if( !st || !st->tree || component_id < 0 )
        return 0;

    int32_t idx = uitree_find_by_component_id(st->tree, component_id);
    if( idx < 0 )
        return 0;

    struct StaticUIElemPosition const* pos = &st->tree->components[idx].position;
    if( !pos->layout_resolved )
        return pos->x;

    int parent_abs_x = 0;
    int32_t parent_idx = st->tree->components[idx].parent;
    if( parent_idx >= 0 )
        parent_abs_x = st->tree->components[parent_idx].position.abs_x;
    return pos->abs_x - parent_abs_x;
}

static int
cs2_host_ui_get_pos_y(
    struct CS2HostUIState const* st,
    int component_id)
{
    if( !st || !st->tree || component_id < 0 )
        return 0;

    int32_t idx = uitree_find_by_component_id(st->tree, component_id);
    if( idx < 0 )
        return 0;

    struct StaticUIElemPosition const* pos = &st->tree->components[idx].position;
    if( !pos->layout_resolved )
        return pos->y;

    int parent_abs_y = 0;
    int32_t parent_idx = st->tree->components[idx].parent;
    if( parent_idx >= 0 )
        parent_abs_y = st->tree->components[parent_idx].position.abs_y;
    return pos->abs_y - parent_abs_y;
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
cs2_host_ui_apply_text_font(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    int font_id)
{
    if( !st || !st->tree )
        return;
    int32_t idx = cs2_host_ui_resolve_target(st, component_id, active_component);
    if( idx < 0 || st->tree->components[idx].type != UIELEM_RS_TEXT )
        return;
    st->tree->components[idx].u.rs_text.font_id = font_id;
    uitree_mark_node_dirty(st->tree, idx);
}

static void
cs2_host_ui_apply_text_align(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    int x_align,
    int y_align,
    int line_height)
{
    if( !st || !st->tree )
        return;
    int32_t idx = cs2_host_ui_resolve_target(st, component_id, active_component);
    if( idx < 0 || st->tree->components[idx].type != UIELEM_RS_TEXT )
        return;
    st->tree->components[idx].u.rs_text.center = x_align;
    st->tree->components[idx].u.rs_text.y_align = y_align;
    st->tree->components[idx].u.rs_text.line_height = line_height;
    uitree_mark_node_dirty(st->tree, idx);
}

static void
cs2_host_ui_apply_text_shadow(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    int shadow)
{
    if( !st || !st->tree )
        return;
    int32_t idx = cs2_host_ui_resolve_target(st, component_id, active_component);
    if( idx < 0 || st->tree->components[idx].type != UIELEM_RS_TEXT )
        return;
    st->tree->components[idx].u.rs_text.shadowed = shadow ? 1 : 0;
    uitree_mark_node_dirty(st->tree, idx);
}

static void
cs2_host_ui_apply_fill(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    int filled)
{
    if( !st || !st->tree )
        return;
    int32_t idx = cs2_host_ui_resolve_target(st, component_id, active_component);
    if( idx < 0 || st->tree->components[idx].type != UIELEM_RS_RECT )
        return;
    st->tree->components[idx].u.rs_rect.filled = filled ? 1 : 0;
    uitree_mark_node_dirty(st->tree, idx);
}

static void
cs2_host_ui_apply_trans(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    int trans)
{
    if( !st || !st->tree )
        return;
    int32_t idx = cs2_host_ui_resolve_target(st, component_id, active_component);
    if( idx < 0 )
        return;
    st->tree->components[idx].trans = trans;
    uitree_mark_node_dirty(st->tree, idx);
}

static void
cs2_host_ui_apply_no_click_through(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    int no_click_through)
{
    if( !st || !st->tree )
        return;
    int32_t idx = cs2_host_ui_resolve_target(st, component_id, active_component);
    if( idx < 0 )
        return;
    st->tree->components[idx].no_click_through = no_click_through ? 1 : 0;
    uitree_mark_node_dirty(st->tree, idx);
}

static void
cs2_host_ui_apply_draggable(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    int parent_uid,
    int child_index)
{
    (void)parent_uid;
    (void)child_index;
    if( !st || !st->tree )
        return;
    int32_t idx = cs2_host_ui_resolve_target(st, component_id, active_component);
    if( idx < 0 )
        return;
    st->tree->components[idx].draggable = 1;
    uitree_mark_node_dirty(st->tree, idx);
}

static void
cs2_host_ui_apply_draggable_behavior(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    int behavior)
{
    if( !st || !st->tree )
        return;
    int32_t idx = cs2_host_ui_resolve_target(st, component_id, active_component);
    if( idx < 0 )
        return;
    st->tree->components[idx].drag_behavior = behavior;
    uitree_mark_node_dirty(st->tree, idx);
}

static void
cs2_host_ui_apply_drag_dead_zone(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    int zone)
{
    if( !st || !st->tree )
        return;
    int32_t idx = cs2_host_ui_resolve_target(st, component_id, active_component);
    if( idx < 0 )
        return;
    st->tree->components[idx].drag_dead_zone = (uint8_t)zone;
    uitree_mark_node_dirty(st->tree, idx);
}

static void
cs2_host_ui_apply_drag_dead_time(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    int time)
{
    if( !st || !st->tree )
        return;
    int32_t idx = cs2_host_ui_resolve_target(st, component_id, active_component);
    if( idx < 0 )
        return;
    st->tree->components[idx].drag_dead_time = (uint8_t)time;
    uitree_mark_node_dirty(st->tree, idx);
}

static void
cs2_host_ui_apply_op(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    int index,
    char const* text)
{
    if( !st || !st->tree || index < 1 || index > UITREE_MENU_OPTION_SLOTS )
        return;
    int32_t idx = cs2_host_ui_resolve_target(st, component_id, active_component);
    if( idx < 0 )
        return;
    strncpy(
        st->tree->components[idx].menu_options.ops[index - 1],
        text ? text : "",
        UITREE_MENU_OPTION_LEN - 1);
    st->tree->components[idx].menu_options.ops[index - 1][UITREE_MENU_OPTION_LEN - 1] = '\0';
    uitree_mark_node_dirty(st->tree, idx);
}

static void
cs2_host_ui_push_cstring(
    struct CS2_InvokeCtx* ctx,
    char const* text)
{
    cs2vm_host_push_string(ctx, cs2vm_host_alloc_string(ctx, text ? text : ""));
}

static void
cs2_host_ui_push_component_op(
    struct CS2HostUIState* st,
    struct CS2_InvokeCtx* ctx,
    int component_id,
    int active_component,
    int op_index)
{
    char buf[UITREE_MENU_OPTION_LEN];
    if( st && st->tree )
        (void)uitree_get_op(
            st->tree, component_id, active_component, op_index, buf, (int)sizeof(buf));
    else
        buf[0] = '\0';
    cs2_host_ui_push_cstring(ctx, buf);
}

static void
cs2_host_ui_push_component_op_base(
    struct CS2HostUIState* st,
    struct CS2_InvokeCtx* ctx,
    int component_id,
    int active_component)
{
    char buf[UITREE_MENU_OPTION_LEN];
    if( st && st->tree )
        (void)uitree_get_op_base(st->tree, component_id, active_component, buf, (int)sizeof(buf));
    else
        buf[0] = '\0';
    cs2_host_ui_push_cstring(ctx, buf);
}

static void
cs2_host_ui_push_component_text(
    struct CS2HostUIState* st,
    struct CS2_InvokeCtx* ctx,
    int component_id,
    int active_component)
{
    char buf[256];
    if( st && st->tree )
        (void)uitree_get_text(st->tree, component_id, active_component, buf, (int)sizeof(buf));
    else
        buf[0] = '\0';
    cs2_host_ui_push_cstring(ctx, buf);
}

static int
cs2_host_ui_get_scroll_height(
    struct CS2HostUIState const* st,
    int component_id)
{
    if( !st || !st->tree || component_id < 0 )
        return 0;
    int32_t idx = uitree_find_by_component_id(st->tree, component_id);
    if( idx < 0 || st->tree->components[idx].type != UIELEM_RS_LAYER )
        return 0;
    return st->tree->components[idx].u.rs_layer.scroll_height;
}

static int
cs2_host_ui_get_scroll_width(
    struct CS2HostUIState const* st,
    int component_id)
{
    if( !st || !st->tree || component_id < 0 )
        return 0;
    int32_t idx = uitree_find_by_component_id(st->tree, component_id);
    if( idx < 0 || st->tree->components[idx].type != UIELEM_RS_LAYER )
        return 0;
    return st->tree->components[idx].u.rs_layer.scroll_width;
}

static int
cs2_host_ui_get_hide(
    struct CS2HostUIState const* st,
    int component_id)
{
    if( !st || !st->tree || component_id < 0 )
        return 0;

    int32_t idx = uitree_find_by_component_id(st->tree, component_id);
    if( idx < 0 )
        return 0;

    return st->tree->components[idx].behavior.hide ? 1 : 0;
}

static int
cs2_host_ui_oc_unplaceholder(
    struct CS2HostUIState const* st,
    int item_id)
{
    if( item_id <= 0 )
        return item_id;

    struct RSCacheDat2Disk* dat2 = cs2_host_ui_dat2_disk(st);
    if( !dat2 )
        return item_id;

    struct RSCacheDat2Disk_Archive* arch = RSCacheDat2Disk_ArchiveNewLoad(
        dat2, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Object);
    if( !arch )
        return item_id;

    RSCacheDat2Disk_ArchiveInitMetadata(dat2, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl )
    {
        RSCacheDat2Disk_ArchiveFree(arch);
        return item_id;
    }

    struct RSCacheDat2Disk_ReferenceTable* table = dat2->tables[RSCacheDat2Disk_Table_Configs];
    struct RSCacheDat2Disk_ArchiveReference* ref = NULL;
    if( table && table->archives )
        ref = &table->archives[RSCacheDat2A_ConfigKind_Object];

    int result = item_id;
    for( int i = 0; i < fl->file_count; i++ )
    {
        int id = (ref && i < ref->children.count) ? ref->children.files[i].id : i;
        if( id != item_id )
            continue;

        struct RSCacheDat2A_ConfigObject obj;
        memset(&obj, 0, sizeof(obj));
        RSCacheDat2A_ConfigObjectDecodeInplace(&obj, fl->files[i], fl->file_sizes[i]);
        if( obj.placeholder_template_id >= 0 && obj.placeholder_id >= 0 )
            result = obj.placeholder_id;
        RSCacheDat2A_ConfigObjectFree(&obj);
        break;
    }

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);
    return result;
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

static void
cs2_host_ui_set_active(
    struct CS2HostUIState* st,
    struct CS2_InvokeCtx* ctx,
    int component_id)
{
    if( !st || !ctx )
        return;
    cs2vm_host_set_active_component(ctx, component_id);
    st->active_node_index = cs2_host_ui_resolve_target(st, component_id, component_id);
}

static int
cs2_host_ui_cc_target_component(struct CS2_InvokeCtx const* ctx)
{
    if( !ctx )
        return -1;
    return ctx->operand == 1 ? ctx->dot_component : ctx->active_component;
}

static void
cs2_host_ui_set_cc_target(
    struct CS2HostUIState* st,
    struct CS2_InvokeCtx* ctx,
    int int_op,
    int component_id)
{
    if( !st || !ctx )
        return;
    if( int_op == 1 )
        cs2vm_host_set_dot_component(ctx, component_id);
    else
        cs2_host_ui_set_active(st, ctx, component_id);
}

static void
cs2_host_ui_apply_object_op(
    struct CS2HostUIState* st,
    int component_id,
    int active_component,
    int obj_id,
    int obj_count,
    bool always_num)
{
    if( !st || !st->tree || obj_id <= 0 )
        return;

    int scene_id = -1;
    int atlas_index = 0;
    if( st->resolve_obj_icon )
    {
        (void)st->resolve_obj_icon(st->resolve_obj_icon_ud, obj_id, &scene_id, &atlas_index);
    }

    int const target = component_id >= 0 ? component_id : active_component;
    (void)always_num;
    (void)uitree_apply_object(st->tree, target, obj_id, obj_count, scene_id, atlas_index);
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
    {
        /* Modern client (rev >= 200): parentUid, type, childIndex, isNested. */
        int is_nested = cs2vm_host_pop_int(ctx);
        int child_index = cs2vm_host_pop_int(ctx);
        int widget_type = cs2vm_host_pop_int(ctx);
        int parent_uid = cs2vm_host_pop_int(ctx);
        (void)is_nested;

        int const group_id = (parent_uid >> 16) & 0xffff;
        if( group_id == 0 )
        {
            if( st->cc_create_notify )
                st->cc_create_notify(st->cc_create_notify_ud, parent_uid, child_index, 0);
            cs2vm_host_halt(ctx);
            break;
        }

        int32_t parent_idx = cs2_host_ui_resolve_target(st, parent_uid, parent_uid);
        if( parent_idx < 0 )
        {
            if( cs2vm_get_trace() )
            {
                fprintf(
                    stderr,
                    "cs2_host_ui: CC_CREATE failed parent unresolved parent=0x%x sub=%d type=%d\n",
                    (unsigned)parent_uid,
                    child_index,
                    widget_type);
            }
            if( st->cc_create_notify )
                st->cc_create_notify(st->cc_create_notify_ud, parent_uid, child_index, 0);
            cs2vm_host_halt(ctx);
            break;
        }
        int32_t child_idx =
            uitree_cc_create(st->tree, parent_idx, parent_uid, widget_type, child_index);
        if( child_idx < 0 )
        {
            if( cs2vm_get_trace() )
            {
                fprintf(
                    stderr,
                    "cs2_host_ui: CC_CREATE failed parent_idx=%d parent=0x%x sub=%d type=%d\n",
                    (int)parent_idx,
                    (unsigned)parent_uid,
                    child_index,
                    widget_type);
            }
            if( st->cc_create_notify )
                st->cc_create_notify(st->cc_create_notify_ud, parent_uid, child_index, 0);
            cs2vm_host_halt(ctx);
            break;
        }
        int child_id = st->tree->components[child_idx].component_id;
        cs2_host_ui_set_cc_target(st, ctx, ctx->operand, child_id);
        if( ctx->operand != 1 )
            st->active_node_index = child_idx;
        if( cs2vm_get_trace() )
        {
            fprintf(
                stderr,
                "cs2_host_ui: CC_CREATE ok parent=0x%x sub=%d child=0x%x type=%d\n",
                (unsigned)parent_uid,
                child_index,
                (unsigned)child_id,
                widget_type);
        }
        if( st->cc_create_notify )
            st->cc_create_notify(st->cc_create_notify_ud, parent_uid, child_index, 1);
        break;
    }
    case CS2_OP_CC_DELETEALL:
    {
        int parent_uid = cs2vm_host_pop_int(ctx);
        int32_t parent_idx = cs2_host_ui_resolve_target(st, parent_uid, parent_uid);
        if( parent_idx >= 0 )
            uitree_cc_delete_all(st->tree, parent_idx);
        break;
    }
    case CS2_OP_CC_DELETE:
        break;
    case CS2_OP_IF_FIND_CHILD:
    {
        int value2_type = cs2vm_host_pop_int(ctx);
        int value1_type = cs2vm_host_pop_int(ctx);
        int value2 = value2_type == 0 ? cs2vm_host_pop_int(ctx) : 0;
        if( value2_type != 0 )
            (void)cs2vm_host_pop_string(ctx);
        int param2_id = cs2vm_host_pop_int(ctx);
        int value1 = value1_type == 0 ? cs2vm_host_pop_int(ctx) : 0;
        if( value1_type != 0 )
            (void)cs2vm_host_pop_string(ctx);
        int param1_id = cs2vm_host_pop_int(ctx);
        int parent_uid = cs2vm_host_pop_int(ctx);
        int32_t parent_idx = cs2_host_ui_resolve_target(st, parent_uid, parent_uid);
        int found = 0;
        if( parent_idx >= 0 && st->tree )
        {
            for( int32_t child = st->tree->components[parent_idx].first_child; child >= 0;
                 child = st->tree->components[child].next_sibling )
            {
                struct StaticUIComponent* c = &st->tree->components[child];
                if( c->dynamic )
                    continue;
                if( param1_id >= 0 && c->behavior.client_code != value1 )
                    continue;
                if( param2_id >= 0 && c->behavior.client_code != value2 )
                    continue;
                cs2_host_ui_set_active(st, ctx, c->component_id);
                found = 1;
                break;
            }
            if( !found && (parent_uid & 0xffff) == 1 )
            {
                for( int32_t child = st->tree->components[parent_idx].first_child; child >= 0;
                     child = st->tree->components[child].next_sibling )
                {
                    struct StaticUIComponent* c = &st->tree->components[child];
                    if( c->dynamic )
                        continue;
                    if( (c->component_id & 0xffff) == 35 )
                    {
                        cs2_host_ui_set_active(st, ctx, c->component_id);
                        found = 1;
                        break;
                    }
                }
            }
        }
        cs2vm_host_push_int(ctx, found);
        break;
    }
    case CS2_OP_CC_FIND:
    {
        int sub = cs2vm_host_pop_int(ctx);
        int parent = cs2vm_host_pop_int(ctx);
        int32_t parent_idx = cs2_host_ui_resolve_target(st, parent, parent);
        int found = 0;
        if( parent_idx >= 0 )
        {
            int32_t child_idx = uitree_find_child_by_subid(st->tree, parent_idx, parent, sub);
            if( child_idx >= 0 )
            {
                int child_id = st->tree->components[child_idx].component_id;
                cs2_host_ui_set_cc_target(st, ctx, ctx->operand, child_id);
                found = 1;
            }
        }
        cs2vm_host_push_int(ctx, found);
        break;
    }
    case CS2_OP_IF_FIND:
    {
        int component = cs2vm_host_pop_int(ctx);
        int32_t idx = cs2_host_ui_resolve_target(st, component, component);
        if( idx >= 0 )
            cs2_host_ui_set_active(st, ctx, component);
        cs2vm_host_push_int(ctx, idx >= 0);
        break;
    }
    case CS2_OP_CC_SETHIDE:
    {
        int hide = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_hide(st, -1, cs2_host_ui_cc_target_component(ctx), hide);
        break;
    }
    case CS2_OP_CC_SETTEXT:
    {
        char* text = cs2vm_host_pop_string(ctx);
        cs2_host_ui_apply_text(st, -1, cs2_host_ui_cc_target_component(ctx), text ? text : "");
        break;
    }
    case CS2_OP_CC_SETTEXTFONT:
    {
        int font_id = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_text_font(st, -1, cs2_host_ui_cc_target_component(ctx), font_id);
        break;
    }
    case CS2_OP_CC_SETTEXTALIGN:
    {
        int line_height = cs2vm_host_pop_int(ctx);
        int y_align = cs2vm_host_pop_int(ctx);
        int x_align = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_text_align(st, -1, cs2_host_ui_cc_target_component(ctx), x_align, y_align, line_height);
        break;
    }
    case CS2_OP_CC_SETTEXTSHADOW:
    {
        int shadow = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_text_shadow(st, -1, cs2_host_ui_cc_target_component(ctx), shadow);
        break;
    }
    case CS2_OP_CC_SETCOLOUR:
    {
        int colour = cs2vm_host_pop_int(ctx);
        if( st->tree )
            (void)uitree_apply_colour(st->tree, cs2_host_ui_cc_target_component(ctx), colour);
        break;
    }
    case CS2_OP_CC_SETFILL:
    {
        int filled = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_fill(st, -1, cs2_host_ui_cc_target_component(ctx), filled);
        break;
    }
    case CS2_OP_CC_SETTRANS:
    {
        int trans = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_trans(st, -1, cs2_host_ui_cc_target_component(ctx), trans);
        break;
    }
    case CS2_OP_CC_SETNOCLICKTHROUGH:
    {
        int no_click_through = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_no_click_through(st, -1, cs2_host_ui_cc_target_component(ctx), no_click_through);
        break;
    }
    case CS2_OP_CC_SETOP:
    {
        char* text = cs2vm_host_pop_string(ctx);
        int index = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_op(st, -1, cs2_host_ui_cc_target_component(ctx), index, text ? text : "");
        break;
    }
    case CS2_OP_CC_SETOPBASE:
    {
        char* text = cs2vm_host_pop_string(ctx);
        if( st->tree )
            (void)uitree_apply_op_base(st->tree, cs2_host_ui_cc_target_component(ctx), text ? text : "");
        break;
    }
    case CS2_OP_CC_SETTARGETVERB:
        (void)cs2vm_host_pop_string(ctx);
        break;
    case CS2_OP_CC_CLEAROPS:
        if( st->tree )
            (void)uitree_clear_ops(st->tree, -1, cs2_host_ui_cc_target_component(ctx));
        break;
    case CS2_OP__1308:
    case CS2_OP__1309:
        (void)cs2vm_host_pop_int(ctx);
        break;
    case CS2_OP_CC_CLEAROPSUBMENU:
        (void)cs2vm_host_pop_int(ctx);
        break;
    case CS2_OP_CC_SETOPSUBMENU:
    {
        (void)cs2vm_host_pop_string(ctx);
        (void)cs2vm_host_pop_int(ctx);
        (void)cs2vm_host_pop_int(ctx);
        break;
    }
    case CS2_OP_CC_SETDRAGGABLE:
    {
        int child_index = cs2vm_host_pop_int(ctx);
        int parent_uid = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_draggable(st, -1, cs2_host_ui_cc_target_component(ctx), parent_uid, child_index);
        break;
    }
    case CS2_OP_CC_SETDRAGGABLEBEHAVIOR:
    {
        int behavior = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_draggable_behavior(st, -1, cs2_host_ui_cc_target_component(ctx), behavior);
        break;
    }
    case CS2_OP_CC_SETDRAGDEADZONE:
    {
        int zone = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_drag_dead_zone(st, -1, cs2_host_ui_cc_target_component(ctx), zone);
        break;
    }
    case CS2_OP_CC_SETDRAGDEADTIME:
    {
        int time = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_drag_dead_time(st, -1, cs2_host_ui_cc_target_component(ctx), time);
        break;
    }
    case CS2_OP_CC_SETGRAPHIC:
    {
        int graphic = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_graphic(st, -1, cs2_host_ui_cc_target_component(ctx), graphic);
        break;
    }
    case CS2_OP_CC_SETPOSITION:
    {
        int y_mode = cs2vm_host_pop_int(ctx);
        int x_mode = cs2vm_host_pop_int(ctx);
        int y = cs2vm_host_pop_int(ctx);
        int x = cs2vm_host_pop_int(ctx);
        if( st->tree )
            (void)uitree_apply_position_modes(
                st->tree, cs2_host_ui_cc_target_component(ctx), x, y, x_mode, y_mode);
        break;
    }
    case CS2_OP_CC_SETSIZE:
    {
        int height_mode = cs2vm_host_pop_int(ctx);
        int width_mode = cs2vm_host_pop_int(ctx);
        int height = cs2vm_host_pop_int(ctx);
        int width = cs2vm_host_pop_int(ctx);
        if( st->tree )
            (void)uitree_apply_size_modes(
                st->tree, cs2_host_ui_cc_target_component(ctx), width, height, width_mode, height_mode);
        break;
    }
    case CS2_OP_CC_SETTILING:
    {
        int tiled = cs2vm_host_pop_int(ctx);
        if( st->tree )
            (void)uitree_apply_graphic_tiled(st->tree, cs2_host_ui_cc_target_component(ctx), tiled);
        break;
    }
    case CS2_OP_CC_SETOUTLINE:
    {
        int outline = cs2vm_host_pop_int(ctx);
        if( st->tree )
            (void)uitree_apply_graphic_outline(st->tree, cs2_host_ui_cc_target_component(ctx), outline);
        break;
    }
    case CS2_OP_CC_SETGRAPHICSHADOW:
    {
        int shadow = cs2vm_host_pop_int(ctx);
        if( st->tree )
            (void)uitree_apply_graphic_shadow(st->tree, cs2_host_ui_cc_target_component(ctx), shadow);
        break;
    }
    case CS2_OP_CC_GETID:
        cs2vm_host_push_int(ctx, cs2_host_ui_cc_target_component(ctx));
        break;
    case CS2_OP_CC_PARAM:
    {
        int param_id = cs2vm_host_pop_int(ctx);
        struct RSCacheDat2Disk* dat2 = cs2_host_ui_dat2_disk(st);
        if( dat2 && ie_param_is_string(dat2, param_id) )
            cs2vm_host_push_string(
                ctx, cs2vm_host_alloc_string(ctx, ie_param_default_string(dat2, param_id)));
        else
            cs2vm_host_push_int(ctx, dat2 ? ie_param_default_int(dat2, param_id) : 0);
        break;
    }
    case CS2_OP_CC_GETX:
        cs2vm_host_push_int(ctx, cs2_host_ui_get_pos_x(st, cs2_host_ui_cc_target_component(ctx)));
        break;
    case CS2_OP_CC_GETY:
        cs2vm_host_push_int(ctx, cs2_host_ui_get_pos_y(st, cs2_host_ui_cc_target_component(ctx)));
        break;
    case CS2_OP_CC_GETLAYER:
        cs2vm_host_push_int(ctx, cs2_host_ui_get_layer(st, cs2_host_ui_cc_target_component(ctx)));
        break;
    case CS2_OP_CC_GETOP:
    {
        int op_index = cs2vm_host_pop_int(ctx);
        cs2_host_ui_push_component_op(st, ctx, -1, cs2_host_ui_cc_target_component(ctx), op_index);
        break;
    }
    case CS2_OP_CC_GETOPBASE:
        cs2_host_ui_push_component_op_base(st, ctx, -1, cs2_host_ui_cc_target_component(ctx));
        break;
    case CS2_OP_CC_GETTEXT:
        cs2_host_ui_push_component_text(st, ctx, -1, cs2_host_ui_cc_target_component(ctx));
        break;
    case CS2_OP_CC_GETWIDTH:
    {
        int width = 0;
        if( st->tree )
            width = uitree_get_layout_width(st->tree, cs2_host_ui_cc_target_component(ctx));
        cs2vm_host_push_int(ctx, width);
        break;
    }
    case CS2_OP_CC_GETHEIGHT:
    {
        int height = 0;
        if( st->tree )
            height = uitree_get_layout_height(st->tree, cs2_host_ui_cc_target_component(ctx));
        cs2vm_host_push_int(ctx, height);
        break;
    }
    case CS2_OP_CC_GETHIDE:
        cs2vm_host_push_int(ctx, cs2_host_ui_get_hide(st, cs2_host_ui_cc_target_component(ctx)));
        break;
    case CS2_OP_CC_GETSCROLLX:
        cs2vm_host_push_int(ctx, 0);
        break;
    case CS2_OP_CC_GETSCROLLY:
        cs2vm_host_push_int(ctx, 0);
        break;
    case CS2_OP_CC_GETSCROLLWIDTH:
        cs2vm_host_push_int(ctx, cs2_host_ui_get_scroll_width(st, cs2_host_ui_cc_target_component(ctx)));
        break;
    case CS2_OP_CC_GETSCROLLHEIGHT:
        cs2vm_host_push_int(ctx, cs2_host_ui_get_scroll_height(st, cs2_host_ui_cc_target_component(ctx)));
        break;
    case CS2_OP_IF_GETWIDTH:
    {
        int component = cs2vm_host_pop_int(ctx);
        int width = 0;
        if( st->tree )
            width = uitree_get_layout_width(st->tree, component);
        cs2vm_host_push_int(ctx, width);
        break;
    }
    case CS2_OP_IF_GETHEIGHT:
    {
        int component = cs2vm_host_pop_int(ctx);
        int height = 0;
        if( st->tree )
            height = uitree_get_layout_height(st->tree, component);
        cs2vm_host_push_int(ctx, height);
        break;
    }
    case CS2_OP_IF_GETX:
    {
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_push_int(ctx, cs2_host_ui_get_pos_x(st, component));
        break;
    }
    case CS2_OP_IF_GETY:
    {
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_push_int(ctx, cs2_host_ui_get_pos_y(st, component));
        break;
    }
    case CS2_OP_IF_GETLAYER:
    {
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_push_int(ctx, cs2_host_ui_get_layer(st, component));
        break;
    }
    case CS2_OP_IF_GETSCROLLHEIGHT:
    {
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_push_int(ctx, cs2_host_ui_get_scroll_height(st, component));
        break;
    }
    case CS2_OP_IF_GETHIDE:
    {
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_push_int(ctx, cs2_host_ui_get_hide(st, component));
        break;
    }
    case CS2_OP_IF_GETSCROLLX:
    {
        (void)cs2vm_host_pop_int(ctx);
        cs2vm_host_push_int(ctx, 0);
        break;
    }
    case CS2_OP_IF_GETSCROLLY:
    {
        int component = cs2vm_host_pop_int(ctx);
        (void)component;
        cs2vm_host_push_int(ctx, 0);
        break;
    }
    case CS2_OP_IF_GETSCROLLWIDTH:
    {
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_push_int(ctx, cs2_host_ui_get_scroll_width(st, component));
        break;
    }
    case CS2_OP_IF_SETNOCLICKTHROUGH:
    {
        int no_click_through = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2_host_ui_apply_no_click_through(st, component, ctx->active_component, no_click_through);
        break;
    }
    case CS2_OP_IF_SETTRANS:
    {
        int trans = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2_host_ui_apply_trans(st, component, ctx->active_component, trans);
        break;
    }
    case CS2_OP_IF_SETOP:
    {
        char* text = cs2vm_host_pop_string(ctx);
        int index = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2_host_ui_apply_op(st, component, ctx->active_component, index, text ? text : "");
        break;
    }
    case CS2_OP_IF_SETDRAGGABLE:
    {
        int child_index = cs2vm_host_pop_int(ctx);
        int parent_uid = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2_host_ui_apply_draggable(st, component, ctx->active_component, parent_uid, child_index);
        break;
    }
    case CS2_OP_IF_SETDRAGGABLEBEHAVIOR:
    {
        int behavior = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2_host_ui_apply_draggable_behavior(st, component, ctx->active_component, behavior);
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
    case CS2_OP_IF_SETTEXTFONT:
    {
        int font_id = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2_host_ui_apply_text_font(st, component, ctx->active_component, font_id);
        break;
    }
    case CS2_OP_IF_SETTEXTALIGN:
    {
        int component = cs2vm_host_pop_int(ctx);
        int line_height = cs2vm_host_pop_int(ctx);
        int y_align = cs2vm_host_pop_int(ctx);
        int x_align = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2_host_ui_apply_text_align(
            st, component, ctx->active_component, x_align, y_align, line_height);
        break;
    }
    case CS2_OP_IF_SETTEXTSHADOW:
    {
        int shadow = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2_host_ui_apply_text_shadow(st, component, ctx->active_component, shadow);
        break;
    }
    case CS2_OP_IF_SETFILL:
    {
        int filled = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2_host_ui_apply_fill(st, component, ctx->active_component, filled);
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
    case CS2_OP_IF_SETOUTLINE:
    {
        int outline = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        if( st->tree )
            (void)uitree_apply_graphic_outline(st->tree, component, outline);
        break;
    }
    case CS2_OP_IF_SETPOSITION:
    {
        int component = cs2vm_host_pop_int(ctx);
        int y_mode = cs2vm_host_pop_int(ctx);
        int x_mode = cs2vm_host_pop_int(ctx);
        int y = cs2vm_host_pop_int(ctx);
        int x = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        if( st->tree )
            (void)uitree_apply_position_modes(st->tree, component, x, y, x_mode, y_mode);
        break;
    }
    case CS2_OP_IF_SETSIZE:
    {
        int component = cs2vm_host_pop_int(ctx);
        int height_mode = cs2vm_host_pop_int(ctx);
        int width_mode = cs2vm_host_pop_int(ctx);
        int height = cs2vm_host_pop_int(ctx);
        int width = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        if( st->tree )
            (void)uitree_apply_size_modes(
                st->tree, component, width, height, width_mode, height_mode);
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
    case CS2_OP_CC_SETOBJECT:
    case CS2_OP_CC_SETOBJECT_ALWAYS_NUM:
    {
        int count = cs2vm_host_pop_int(ctx);
        int obj_id = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_object_op(st, -1, cs2_host_ui_cc_target_component(ctx), obj_id, count, true);
        break;
    }
    case CS2_OP_CC_SETOBJECT_NONUM:
    {
        int count = cs2vm_host_pop_int(ctx);
        int obj_id = cs2vm_host_pop_int(ctx);
        cs2_host_ui_apply_object_op(st, -1, cs2_host_ui_cc_target_component(ctx), obj_id, count, false);
        break;
    }
    case CS2_OP_IF_SETOBJECT:
    case CS2_OP_IF_SETOBJECT_ALWAYS_NUM:
    {
        int component = cs2vm_host_pop_int(ctx);
        int count = cs2vm_host_pop_int(ctx);
        int obj_id = cs2vm_host_pop_int(ctx);
        cs2_host_ui_set_active(st, ctx, component);
        cs2_host_ui_apply_object_op(st, component, ctx->active_component, obj_id, count, true);
        break;
    }
    case CS2_OP_IF_SETOBJECT_NONUM:
    {
        int component = cs2vm_host_pop_int(ctx);
        int count = cs2vm_host_pop_int(ctx);
        int obj_id = cs2vm_host_pop_int(ctx);
        cs2_host_ui_set_active(st, ctx, component);
        cs2_host_ui_apply_object_op(st, component, ctx->active_component, obj_id, count, false);
        break;
    }
    case CS2_OP_INV_GETOBJ:
    {
        int slot = cs2vm_host_pop_int(ctx);
        int inv_id = cs2vm_host_pop_int(ctx);
        int obj_id = 0;
        if( st->inv_get_obj )
            obj_id = st->inv_get_obj(st->inv_ud, inv_id, slot);
        cs2vm_host_push_int(ctx, obj_id);
        break;
    }
    case CS2_OP_INV_GETNUM:
    {
        int slot = cs2vm_host_pop_int(ctx);
        int inv_id = cs2vm_host_pop_int(ctx);
        int count = 0;
        if( st->inv_get_num )
            count = st->inv_get_num(st->inv_ud, inv_id, slot);
        cs2vm_host_push_int(ctx, count);
        break;
    }
    case CS2_OP_INV_SIZE:
    {
        int inv_id = cs2vm_host_pop_int(ctx);
        int size = 0;
        if( st->inv_size )
            size = st->inv_size(st->inv_ud, inv_id);
        cs2vm_host_push_int(ctx, size);
        break;
    }
    case CS2_OP_INV_TOTAL:
    {
        int item_id = cs2vm_host_pop_int(ctx);
        int inv_id = cs2vm_host_pop_int(ctx);
        int total = 0;
        if( st->inv_get_obj && st->inv_get_num && st->inv_size && item_id > 0 )
        {
            int const size = st->inv_size(st->inv_ud, inv_id);
            for( int slot = 0; slot < size; slot++ )
            {
                if( st->inv_get_obj(st->inv_ud, inv_id, slot) == item_id )
                    total += st->inv_get_num(st->inv_ud, inv_id, slot);
            }
        }
        cs2vm_host_push_int(ctx, total);
        break;
    }
    case CS2_OP_CC_SETONINVTRANSMIT:
    case CS2_OP_IF_SETONINVTRANSMIT:
        (void)cs2vm_host_pop_string(ctx);
        break;
    case CS2_OP_TOSTRING:
    {
        int val = cs2vm_host_pop_int(ctx);
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", val);
        cs2_host_ui_push_cstring(ctx, buf);
        break;
    }
    case CS2_OP_APPEND:
    {
        char* str2 = cs2vm_host_pop_string(ctx);
        char* str1 = cs2vm_host_pop_string(ctx);
        char buf[512];
        snprintf(buf, sizeof(buf), "%s%s", str1 ? str1 : "", str2 ? str2 : "");
        cs2_host_ui_push_cstring(ctx, buf);
        break;
    }
    case CS2_OP_APPEND_NUM:
    {
        int num = cs2vm_host_pop_int(ctx);
        char* str = cs2vm_host_pop_string(ctx);
        char buf[512];
        snprintf(buf, sizeof(buf), "%s%d", str ? str : "", num);
        cs2_host_ui_push_cstring(ctx, buf);
        break;
    }
    case CS2_OP_APPEND_SIGNNUM:
    {
        int num = cs2vm_host_pop_int(ctx);
        char* str = cs2vm_host_pop_string(ctx);
        char buf[512];
        snprintf(buf, sizeof(buf), "%s%s%d", str ? str : "", num >= 0 ? "+" : "", num);
        cs2_host_ui_push_cstring(ctx, buf);
        break;
    }
    case CS2_OP_APPEND_CHAR:
    {
        int chr = cs2vm_host_pop_int(ctx);
        char* str = cs2vm_host_pop_string(ctx);
        char buf[512];
        size_t len = str ? strlen(str) : 0;
        if( len >= sizeof(buf) - 2 )
            len = sizeof(buf) - 2;
        if( str && len > 0 )
            memcpy(buf, str, len);
        buf[len] = (char)chr;
        buf[len + 1] = '\0';
        cs2_host_ui_push_cstring(ctx, buf);
        break;
    }
    case CS2_OP_CLEARBIT:
    {
        int bit = cs2vm_host_pop_int(ctx);
        int value = cs2vm_host_pop_int(ctx);
        cs2vm_host_push_int(ctx, value & ~(1 << bit));
        break;
    }
    case CS2_OP_SETBIT:
    {
        int bit = cs2vm_host_pop_int(ctx);
        int value = cs2vm_host_pop_int(ctx);
        cs2vm_host_push_int(ctx, value | (1 << bit));
        break;
    }
    case CS2_OP_INTERPOLATE:
    {
        int e = cs2vm_host_pop_int(ctx);
        int d = cs2vm_host_pop_int(ctx);
        int c = cs2vm_host_pop_int(ctx);
        int b = cs2vm_host_pop_int(ctx);
        int a = cs2vm_host_pop_int(ctx);
        int denom = d - c;
        if( denom == 0 )
            cs2vm_host_push_int(ctx, a);
        else
        {
            int mul = (b - a) * (e - c);
            int div = mul / denom;
            cs2vm_host_push_int(ctx, a + div);
        }
        break;
    }
    case CS2_OP_RANDOMINC:
    {
        int max = cs2vm_host_pop_int(ctx);
        if( max < 0 )
            cs2vm_host_push_int(ctx, 0);
        else
            cs2vm_host_push_int(ctx, (int)(rand() % ((unsigned)(max + 1))));
        break;
    }
    case CS2_OP_STRING_LENGTH:
    {
        char* str = cs2vm_host_pop_string(ctx);
        cs2vm_host_push_int(ctx, str ? (int)strlen(str) : 0);
        break;
    }
    case CS2_OP_CLIENTCLOCK:
        cs2vm_host_push_int(ctx, st ? st->clock_ticks : 0);
        break;
    case CS2_OP_COORD:
        cs2vm_host_push_int(ctx, 0);
        break;
    case CS2_OP_COORDX:
    {
        int packed = cs2vm_host_pop_int(ctx);
        cs2vm_host_push_int(ctx, (packed >> 14) & 0x3FFF);
        break;
    }
    case CS2_OP_IF_GETTOP:
        cs2vm_host_push_int(ctx, st && st->client_type > 0 ? st->client_type : 80);
        break;
    case CS2_OP_PARAWIDTH:
    {
        int font_id = cs2vm_host_pop_int(ctx);
        int max_width = cs2vm_host_pop_int(ctx);
        char* str = cs2vm_host_pop_string(ctx);
        int width = 0;
        if( st && st->scene && str && str[0] != '\0' )
        {
            struct ToriDraw_Font* font = ToriDraw_SceneFontGet(st->scene, font_id);
            if( font )
                width = ToriDraw2D_WrapMaxLineWidth(font, str, max_width);
        }
        cs2vm_host_push_int(ctx, width);
        break;
    }
    case CS2_OP_PARAHEIGHT:
    {
        int font_id = cs2vm_host_pop_int(ctx);
        int max_width = cs2vm_host_pop_int(ctx);
        char* str = cs2vm_host_pop_string(ctx);
        int lines = 0;
        if( st && st->scene && str && str[0] != '\0' )
        {
            struct ToriDraw_Font* font = ToriDraw_SceneFontGet(st->scene, font_id);
            if( font )
                lines = ToriDraw2D_WrapLineCount(font, str, max_width);
        }
        cs2vm_host_push_int(ctx, lines);
        break;
    }
    case CS2_OP_SCALE:
    {
        int c = cs2vm_host_pop_int(ctx);
        int b = cs2vm_host_pop_int(ctx);
        int a = cs2vm_host_pop_int(ctx);
        int result = 0;
        if( b != 0 )
            result = (int)(((int64_t)c * (int64_t)a) / (int64_t)b);
        cs2vm_host_push_int(ctx, result);
        break;
    }
    case CS2_OP_OC_NAME:
    {
        int item_id = cs2vm_host_pop_int(ctx);
        char const* name = "null";
        struct RSCacheDat2A_ConfigObject* obj =
            cs2_host_ui_load_object_config(cs2_host_ui_dat2_disk(st), item_id);
        if( obj )
        {
            if( obj->name && obj->name[0] != '\0' )
                name = obj->name;
            RSCacheDat2A_ConfigObjectFree(obj);
        }
        cs2vm_host_push_string(ctx, cs2vm_host_alloc_string(ctx, name));
        break;
    }
    case CS2_OP_OC_UNPLACEHOLDER:
    {
        int item_id = cs2vm_host_pop_int(ctx);
        cs2vm_host_push_int(ctx, cs2_host_ui_oc_unplaceholder(st, item_id));
        break;
    }
    case CS2_OP_OC_PARAM:
    {
        int param_id = cs2vm_host_pop_int(ctx);
        int item_id = cs2vm_host_pop_int(ctx);
        struct RSCacheDat2Disk* dat2 = cs2_host_ui_dat2_disk(st);
        bool const is_string = dat2 && ie_param_is_string(dat2, param_id);
        struct RSCacheDat2A_ConfigObject* obj = cs2_host_ui_load_object_config(dat2, item_id);
        bool found = false;
        if( obj && obj->params.count > 0 )
        {
            for( int i = 0; i < obj->params.count; i++ )
            {
                if( obj->params.keys[i] != param_id )
                    continue;
                found = true;
                if( obj->params.is_string[i] )
                {
                    char const* str =
                        obj->params.values[i] ? (char const*)obj->params.values[i] : "";
                    cs2vm_host_push_string(ctx, cs2vm_host_alloc_string(ctx, str));
                }
                else
                {
                    int val = obj->params.values[i] ? *(int*)obj->params.values[i] : 0;
                    cs2vm_host_push_int(ctx, val);
                }
                break;
            }
        }
        if( obj )
            RSCacheDat2A_ConfigObjectFree(obj);
        if( !found )
        {
            if( is_string )
            {
                cs2vm_host_push_string(
                    ctx,
                    cs2vm_host_alloc_string(
                        ctx, dat2 ? ie_param_default_string(dat2, param_id) : ""));
            }
            else
                cs2vm_host_push_int(ctx, dat2 ? ie_param_default_int(dat2, param_id) : 0);
        }
        break;
    }
    case CS2_OP_IF_HASSUB:
    {
        (void)cs2vm_host_pop_int(ctx);
        cs2vm_host_push_int(ctx, 0);
        break;
    }
    case CS2_OP_CAM_SETFOLLOWHEIGHT:
        (void)cs2vm_host_pop_int(ctx);
        break;
    case CS2_OP_CAM_GETFOLLOWHEIGHT:
        cs2vm_host_push_int(ctx, 0);
        break;
    case CS2_OP_VIEWPORT_SETFOV:
        (void)cs2vm_host_pop_int(ctx);
        (void)cs2vm_host_pop_int(ctx);
        break;
    case CS2_OP_VIEWPORT_SETZOOM:
        (void)cs2vm_host_pop_int(ctx);
        (void)cs2vm_host_pop_int(ctx);
        break;
    case CS2_OP_VIEWPORT_CLAMPFOV:
        (void)cs2vm_host_pop_int(ctx);
        (void)cs2vm_host_pop_int(ctx);
        (void)cs2vm_host_pop_int(ctx);
        (void)cs2vm_host_pop_int(ctx);
        break;
    case CS2_OP_VIEWPORT_GETEFFECTIVESIZE:
    {
        int w = st->viewport_w;
        int h = st->viewport_h;
        if( w > 0 && h > 0 )
        {
            cs2vm_host_push_int(ctx, w);
            cs2vm_host_push_int(ctx, h);
        }
        else
        {
            cs2vm_host_push_int(ctx, -1);
            cs2vm_host_push_int(ctx, -1);
        }
        break;
    }
    case CS2_OP_VIEWPORT_GETZOOM:
        cs2vm_host_push_int(ctx, 128);
        cs2vm_host_push_int(ctx, 896);
        break;
    case CS2_OP_VIEWPORT_GETFOV:
        cs2vm_host_push_int(ctx, 128);
        cs2vm_host_push_int(ctx, 896);
        break;
    case CS2_OP_SETSHOWMOUSEOVERTEXT:
    case CS2_OP_SETSHOWMOUSECROSS:
    case CS2_OP_SETSHOWLOADINGMESSAGES:
    case CS2_OP_SETMOUSECAM:
    case CS2_OP__3130:
    case CS2_OP__3131:
        (void)cs2vm_host_pop_int(ctx);
        break;
    case CS2_OP_RUNWEIGHT_VISIBLE:
        cs2vm_host_push_int(ctx, 0);
        break;
    case CS2_OP_IF_SETMODEL:
    {
        int component = cs2vm_host_pop_int(ctx);
        int model_id = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        if( st->tree )
            (void)uitree_apply_model(st->tree, component, model_id);
        break;
    }
    case CS2_OP_IF_SETMODELTRANSPARENT:
    {
        int component = cs2vm_host_pop_int(ctx);
        int transparent = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        if( st->tree )
            (void)uitree_apply_model_transparent(st->tree, component, transparent);
        break;
    }
    case CS2_OP_IF_SETOPBASE:
    {
        int component = cs2vm_host_pop_int(ctx);
        char* text = cs2vm_host_pop_string(ctx);
        cs2vm_host_set_active_component(ctx, component);
        if( st->tree )
            (void)uitree_apply_op_base(st->tree, component, text ? text : "");
        break;
    }
    case CS2_OP_IF_SETTARGETVERB:
    {
        int component = cs2vm_host_pop_int(ctx);
        (void)cs2vm_host_pop_string(ctx);
        cs2vm_host_set_active_component(ctx, component);
        break;
    }
    case CS2_OP_IF_CLEAROPS:
    {
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        if( st->tree )
            (void)uitree_clear_ops(st->tree, component, ctx->active_component);
        break;
    }
    case CS2_OP__2308:
    case CS2_OP__2309:
    {
        int component = cs2vm_host_pop_int(ctx);
        (void)cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        break;
    }
    case CS2_OP_IF_CLEAROPSUBMENU:
    {
        int component = cs2vm_host_pop_int(ctx);
        (void)cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        break;
    }
    case CS2_OP_IF_SETOPSUBMENU:
    {
        int component = cs2vm_host_pop_int(ctx);
        int sub_index = cs2vm_host_pop_int(ctx);
        int op_index = cs2vm_host_pop_int(ctx);
        char* text = NULL;
        if( !cs2vm_host_try_pop_string(ctx, &text) )
            text = "";
        cs2vm_host_set_active_component(ctx, component);
        (void)sub_index;
        (void)op_index;
        (void)text;
        break;
    }
    case CS2_OP_IF_GETOP:
    {
        int component = cs2vm_host_pop_int(ctx);
        int op_index = cs2vm_host_pop_int(ctx);
        cs2_host_ui_push_component_op(st, ctx, component, -1, op_index);
        break;
    }
    case CS2_OP_IF_GETOPBASE:
    {
        int component = cs2vm_host_pop_int(ctx);
        cs2_host_ui_push_component_op_base(st, ctx, component, -1);
        break;
    }
    case CS2_OP_IF_GETTEXT:
    {
        int component = cs2vm_host_pop_int(ctx);
        cs2_host_ui_push_component_text(st, ctx, component, -1);
        break;
    }
    case CS2_OP_IF_SETDRAGDEADZONE:
    {
        int zone = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2_host_ui_apply_drag_dead_zone(st, component, ctx->active_component, zone);
        break;
    }
    case CS2_OP_IF_SETDRAGDEADTIME:
    {
        int time = cs2vm_host_pop_int(ctx);
        int component = cs2vm_host_pop_int(ctx);
        cs2vm_host_set_active_component(ctx, component);
        cs2_host_ui_apply_drag_dead_time(st, component, ctx->active_component, time);
        break;
    }
    case CS2_OP_MAP_WORLD:
        cs2vm_host_push_int(ctx, 301);
        break;
    case CS2_OP_MAP_MEMBERS:
        cs2vm_host_push_int(ctx, 1);
        break;
    case CS2_OP_ON_MOBILE:
        cs2vm_host_push_int(ctx, 0);
        break;
    case CS2_OP_CLIENTTYPE:
        cs2vm_host_push_int(ctx, 10);
        break;
    case CS2_OP_ENUM_GETOUTPUTCOUNT:
    {
        int enum_id = cs2vm_host_pop_int(ctx);
        int count = 0;
        if( st && st->enum_output_count )
            count = st->enum_output_count(st->enum_output_count_ud, enum_id);
        else
        {
            struct RSCacheDat2Disk* dat2 = cs2_host_ui_dat2_disk(st);
            if( dat2 )
                count = ie_enum_output_count(dat2, enum_id);
        }
        cs2vm_host_push_int(ctx, count);
        break;
    }
    case CS2_OP_STRUCT_PARAM:
    {
        int param_id = cs2vm_host_pop_int(ctx);
        int struct_id = cs2vm_host_pop_int(ctx);
        bool is_string = false;
        int intval = 0;
        char const* strval = NULL;
        bool found = false;
        if( st && st->struct_param )
        {
            found = st->struct_param(
                st->struct_param_ud, struct_id, param_id, &is_string, &intval, &strval);
        }
        else
        {
            struct RSCacheDat2Disk* dat2 = cs2_host_ui_dat2_disk(st);
            if( dat2 )
            {
                found =
                    ie_struct_param_lookup(dat2, struct_id, param_id, &is_string, &intval, &strval);
            }
        }
        if( found )
        {
            if( is_string )
            {
                cs2vm_host_push_string(ctx, cs2vm_host_alloc_string(ctx, strval ? strval : ""));
            }
            else
                cs2vm_host_push_int(ctx, intval);
        }
        else
            cs2vm_host_push_int(ctx, 0);
        break;
    }
    default:
    {
        const struct CS2_OpcodeMeta* meta = cs2_opcode_meta_lookup(ctx->opcode);
        int const script_id = ctx->script ? ctx->script->script_id : -1;
        fprintf(
            stderr,
            "cs2_host_ui: unimplemented opcode %d (%s) script=%d pc=%d active=0x%x\n",
            ctx->opcode,
            meta && meta->name ? meta->name : "?",
            script_id,
            ctx->pc,
            (unsigned)ctx->active_component);
        cs2vm_host_fail(ctx, CS2VM_ERR_UNIMPL);
        break;
    }
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
        s_cs2_host_ui_state.dat2_disk = args->dat2_disk;
        s_cs2_host_ui_state.vm = args->vm;
        s_cs2_host_ui_state.tree = args->tree;
        s_cs2_host_ui_state.on_varp_change = args->on_varp_change;
        s_cs2_host_ui_state.on_varp_change_ud = args->on_varp_change_ud;
        s_cs2_host_ui_state.resolve_obj_icon = args->resolve_obj_icon;
        s_cs2_host_ui_state.resolve_obj_icon_ud = args->resolve_obj_icon_ud;
        s_cs2_host_ui_state.inv_get_obj = args->inv_get_obj;
        s_cs2_host_ui_state.inv_get_num = args->inv_get_num;
        s_cs2_host_ui_state.inv_size = args->inv_size;
        s_cs2_host_ui_state.inv_ud = args->inv_ud;
        s_cs2_host_ui_state.enum_output_count = args->enum_output_count;
        s_cs2_host_ui_state.enum_output_count_ud = args->enum_output_count_ud;
        s_cs2_host_ui_state.struct_param = args->struct_param;
        s_cs2_host_ui_state.struct_param_ud = args->struct_param_ud;
        s_cs2_host_ui_state.cc_create_notify = args->cc_create_notify;
        s_cs2_host_ui_state.cc_create_notify_ud = args->cc_create_notify_ud;
        s_cs2_host_ui_state.scene = args->scene;
        s_cs2_host_ui_state.viewport_w = args->viewport_w;
        s_cs2_host_ui_state.viewport_h = args->viewport_h;
        s_cs2_host_ui_state.client_type = args->client_type > 0 ? args->client_type : 80;
        s_cs2_host_ui_state.active_node_index = -1;
        s_cs2_host_ui_state.clock_ticks = 0;
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

void
cs2_host_ui_tick(struct CS2Host* host)
{
    struct CS2HostUIState* st =
        host && host->ud ? (struct CS2HostUIState*)host->ud : &s_cs2_host_ui_state;
    st->clock_ticks++;
}
