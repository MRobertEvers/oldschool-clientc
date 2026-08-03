#include "game/task_cs2_run.h"

#include "cs2vm2/cs2_opcode_meta.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_host.h"
#include "cs2vm2/cs2vm2_script.h"
#include "engine/cache_provider.h"
#include "engine/task_obj_model_load.h"
#include "engine/torirs_types.h"
#include "engine/uitree_builder/task_pack_assets_load.h"
#include "engine/uitree_from_component.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_cs2_host.h"
#include "net/rev/revpacket.h"
#include "ui/uitree.h"
#include "varp/varp_manager.h"
#include "ui/uitree_layout.h"
#include <3rd/minipt.h>

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UITREE_CLICK_DEBUG
#define UITREE_CLICK_DEBUG 0
#endif

#define TASK_CS2_RUN_INT_ARGS_MAX 64
#define TASK_CS2_RUN_STR_ARGS_MAX 4
/*
 * A clientscript string argument is not a label — it can be a payload.
 *
 * 80 was sized for the things a hook passes (a name, a verb, an item label) and
 * is far too small for the things the *server* passes. rev 230's multi-choice
 * dialogue hands `chatbox_multi_init` its entire option list as one string with
 * the rows joined by `|`; Hans's three-way choice is 132 characters and a
 * five-option list is comfortably past 300.
 *
 * The failure mode is why this is worth a comment rather than a bigger number:
 * the truncated list still *parses*. The clientscript splits on `|`, counts what
 * survived, and lays out that many rows — so a three-option question renders as
 * a tidy, correct, two-option one with the second option cut off mid-word. There
 * is no error at any layer, and the two other caps on the same value
 * (`PKT_RUNCLIENTSCRIPT_STR_LEN`, the encoder's packet size) each hide the next
 * one until they are raised in step.
 */
#define TASK_CS2_RUN_STR_ARG_LEN 512

/*
 * The two caps a server-driven clientscript string passes through, tied
 * together so they cannot be raised out of step.
 *
 * `PKT_RUNCLIENTSCRIPT_STR_LEN` is what the packet parser keeps; this is what
 * the task carries it in. If this is the smaller of the two, the packet arrives
 * whole and the *task* silently trims it — which is the harder of the two
 * failures to find, because the wire trace shows the full string.
 */
_Static_assert(TASK_CS2_RUN_STR_ARG_LEN >= PKT_RUNCLIENTSCRIPT_STR_LEN,
               "a clientscript string argument must survive the packet that carried it");

enum TaskCS2YieldPlan
{
    TASK_CS2_YIELD_NONE = 0,
    TASK_CS2_YIELD_SCRIPT,
    TASK_CS2_YIELD_ENUM,
    TASK_CS2_YIELD_STRUCT,
    TASK_CS2_YIELD_OBJ,
    TASK_CS2_YIELD_COMPONENT,
    TASK_CS2_YIELD_MODEL,
    TASK_CS2_YIELD_NPC,
    TASK_CS2_YIELD_SETOBJECT,
    TASK_CS2_YIELD_SPRITE,
    TASK_CS2_YIELD_FONT,
    TASK_CS2_YIELD_WORLDMAP,
    TASK_CS2_YIELD_MAPELEMENT,
    TASK_CS2_YIELD_OBJALL,
    TASK_CS2_YIELD_DBROW,
    TASK_CS2_YIELD_DBINDEX,
    TASK_CS2_YIELD_DBTABLE,
    TASK_CS2_YIELD_ABORT,
};

struct Task_CS2Run
{
    struct ToriRS_Task task;
    struct pt pt;

    struct RS_CS2Host* host;
    struct CacheProvider* provider;

    int script_id;
    struct CS2VM2_Script* script; /* optional preloaded; else load by script_id */
    int active_component_id;
    int dot_component_id;
    int int_args[TASK_CS2_RUN_INT_ARGS_MAX];
    int int_arg_count;
    /* Bit i set = arg position i is a string; strings fill str_args[] in
     * position order. Positions past the pool cap degrade to "". */
    uint64_t str_mask;
    int str_arg_count;
    char str_args[TASK_CS2_RUN_STR_ARGS_MAX][TASK_CS2_RUN_STR_ARG_LEN];

    /*
     * Event context frozen at CreateTask time.
     *
     * DispatchHook / the intent loop set host->event_* then enqueue; the task
     * only runs once the serial FIFO reaches it. Without a snapshot, every
     * queued onKey for one printable key (code event then character event)
     * would read the last SetEventKey — both append the character, which is
     * the world-map search double-input. The same overwrite hits event_op /
     * event_mouse when two intents share a frame, and a yield for IO loses the
     * whole live context. WIDGET_ID / WIDGET_CHILD_INDEX stay late-bound
     * against the live tree: those are identity lookups, not event payloads.
     */
    int event_key_typed;
    int event_key_pressed;
    int event_mouse_x;
    int event_mouse_y;
    int event_op_index;
    int event_op_subindex;
    int event_drag_target_id;
    int event_drag_target_child_index;

    struct CS2VM_HostRequest pending;
    enum TaskCS2YieldPlan yield_plan;
    int await_id;
    /* Second config a request needs alongside await_id (the ParamType behind a
     * struct/obj param lookup), or -1. One yield loads both. */
    int await_id2;
    int yield_obj_id;
    int yield_obj_count;
    int started;

    /*
     * ~2.9 MB, and deliberately not inline.
     *
     * A queued task is a task that has not started: it is waiting behind the
     * head of a serial pipeline, and until it runs there is nothing for a VM to
     * hold. Inline, every one of those cost 2.9 MB while doing nothing, which
     * is invisible when the pipeline drains inside a frame and fatal when it
     * does not — the browser host takes a network round trip per cache read,
     * and a boot that queues a thousand hook scripts behind that ran a 250 MB
     * native footprint past a 4 GB wasm heap.
     *
     * So it is allocated on the first Run and released with the task. Still no
     * zeroing: CS2VM2_Init sets up the little of it that is load-bearing, which
     * is why this is malloc and not calloc.
     */
    struct CS2VM2* vm;
};

static int
task_cs2_resolve_sprite(
    void* ud,
    int graphic_id)
{
    struct UITreeSceneBridge* bridge = (struct UITreeSceneBridge*)ud;
    assert(bridge);
    if( graphic_id <= 0 )
        return -1;
    return UITreeSceneBridge_EnsureSprite(bridge, graphic_id);
}

static int
task_cs2_resolve_font(
    void* ud,
    int font_id)
{
    struct UITreeSceneBridge* bridge = (struct UITreeSceneBridge*)ud;
    assert(bridge);
    if( font_id < 0 )
        return -1;
    return UITreeSceneBridge_EnsureFont(bridge, font_id);
}

static void
task_cs2_set_int_local(
    struct Task_CS2Run* self,
    struct CS2VM2_Thread* thread,
    int local_idx,
    int argi)
{
    struct RS_CS2Host* host;

    assert(self);
    assert(self->host);
    host = self->host;
    switch( argi )
    {
    case CS2VM_SCRIPT_ARG_WIDGET_ID:
    {
        /*
         * `event_com` is the component's ADDRESS, not its runtime identity: for
         * a dynamic child that is the PARENT's packed `(interface << 16) | child`,
         * with `event_comsubid` carrying the index within it. It is the same
         * (container, sub) pair `app_if_button_target` puts on the wire, and for
         * the same reason — a dynamic child's own component id is a runtime
         * allocation nothing outside this process has a name for.
         *
         * The cache says so plainly, because the pair is what its own scripts
         * feed straight back into `cc_find`:
         *
         *     cc_setonvartransmit("script409(event_com, event_comsubid){var661}")
         *     [clientscript,script409](component $com, int $sub)
         *         if (cc_find($com, $sub) = ^true) { ~script410; }
         *
         * and `~script410` is a bare `cc_settext(...)` on whatever `cc_find`
         * just made active. With `event_com` reported as the child's own id the
         * `cc_find` asks a leaf for a child it does not have, answers false, and
         * the repaint silently does nothing — Slayer Rewards' points header sat
         * at its build-time value through every purchase. `script412` (the
         * unlock tiles) and `script85` (every hover highlight in the game) pass
         * `event_com` beside a *sibling's* `cc_getid` and are unreadable under
         * any other convention.
         */
        int active_component_id = self->active_component_id;
        int32_t idx = UITree_FindByComponentId(host->tree, active_component_id);
        int com = active_component_id;
        if( idx >= 0 && host->tree->components[idx].dynamic )
        {
            int32_t parent = host->tree->components[idx].parent;
            if( parent >= 0 && (uint32_t)parent < host->tree->component_count )
                com = host->tree->components[parent].component_id;
        }
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, com);
        break;
    }
    case CS2VM_SCRIPT_ARG_OP_INDEX:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, self->event_op_index);
        break;
    case CS2VM_SCRIPT_ARG_MOUSE_X:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, self->event_mouse_x);
        break;
    case CS2VM_SCRIPT_ARG_MOUSE_Y:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, self->event_mouse_y);
        break;
    case CS2VM_SCRIPT_ARG_WIDGET_CHILD_INDEX:
    {
        int32_t idx = UITree_FindByComponentId(host->tree, self->active_component_id);
        int child = -1;
        if( idx >= 0 && host->tree->components[idx].dynamic )
            child = host->tree->components[idx].dynamic_child_index;
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, child);
        break;
    }
    case CS2VM_SCRIPT_ARG_DRAG_TARGET_ID:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, self->event_drag_target_id);
        break;
    case CS2VM_SCRIPT_ARG_DRAG_TARGET_CHILD_INDEX:
        CS2VM2_SetIntCurrentFrameLocal(
            thread, local_idx, self->event_drag_target_child_index);
        break;
    /* See struct LibToriRS_KeyEvent: key_typed is the OSRS key CODE and
     * key_pressed the typed CHARACTER, inverted vs canonical OSRS naming but
     * consistent with the reference at both ends. */
    case CS2VM_SCRIPT_ARG_KEY_TYPED:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, self->event_key_typed);
        break;
    case CS2VM_SCRIPT_ARG_KEY_PRESSED:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, self->event_key_pressed);
        break;
    case CS2VM_SCRIPT_ARG_OP_SUBINDEX:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, self->event_op_subindex);
        break;
    default:
        CS2VM2_SetIntCurrentFrameLocal(thread, local_idx, argi);
        break;
    }
}

static int
task_cs2_group_id_from_request(struct CS2VM_HostRequest const* request)
{
    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_CC_CREATE:
        return (request->u.cc_create.parent_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_CC_COPY:
        return (request->u.cc_copy.parent_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_CC_FIND:
        return (request->u.cc_find.parent_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_IF_FIND:
        return (request->u.if_find.component_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_CC_CHILDREN_FIND:
        return (request->u.cc_children_find.parent_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_IF_CHILDREN_FIND:
        return (request->u.if_children_find.uid >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_WIDGET_SET_INT:
        return (request->u.widget_set_int.component_id >> 16) & 0xffff;
    default:
        return -1;
    }
}

/** Parent component id that triggered a missing-group yield, or -1. */
static int
task_cs2_mount_parent_id_from_request(struct CS2VM_HostRequest const* request)
{
    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_CC_CREATE:
        return request->u.cc_create.parent_id;
    case CS2VM_HOST_REQUEST_CC_COPY:
        return request->u.cc_copy.parent_id;
    case CS2VM_HOST_REQUEST_CC_FIND:
        return request->u.cc_find.parent_id;
    case CS2VM_HOST_REQUEST_IF_FIND:
        return request->u.if_find.component_id;
    case CS2VM_HOST_REQUEST_CC_CHILDREN_FIND:
        return request->u.cc_children_find.parent_id;
    case CS2VM_HOST_REQUEST_IF_CHILDREN_FIND:
        return request->u.if_children_find.uid;
    case CS2VM_HOST_REQUEST_WIDGET_SET_INT:
        return request->u.widget_set_int.component_id;
    default:
        return -1;
    }
}

static void
task_cs2_bake_pack(struct Task_CS2Run* self)
{
    struct ToriRS_ComponentPack* pack;
    struct UITree* tree;
    int (*resolve_sprite)(void*, int) = NULL;
    int (*resolve_font)(void*, int) = NULL;
    void* resolve_ud = NULL;
    int pack_root_id;
    int32_t pack_root_idx;
    int mount_parent_id;
    int mount_group;

    assert(self);
    assert(self->host);
    assert(self->host->tree);
    assert(self->await_id > 0);

    tree = self->host->tree;

    if( !CacheProvider_ComponentPackHas(self->provider, self->await_id) )
    {
        fprintf(
            stderr,
            "Task_CS2Run: component pack %d missing after load (script %d)\n",
            self->await_id,
            self->script_id);
        return;
    }

    pack = CacheProvider_ComponentPackGet(self->provider, self->await_id);
    assert(pack);

    if( self->host->bridge )
    {
        resolve_ud = self->host->bridge;
        resolve_sprite = task_cs2_resolve_sprite;
        resolve_font = task_cs2_resolve_font;
    }
    (void)UITree_BuildFromComponentPack(
        tree, pack, resolve_sprite, resolve_font, resolve_ud);

    /* If the yield parent lives on another already-baked group, mount this pack
     * root under it (sub-interface style). Same-group parents (e.g. CC_CREATE
     * under 728:6 while loading 728) are already linked inside the pack. */
    pack_root_id = (self->await_id << 16) | 0;
    pack_root_idx = UITree_FindByComponentId(tree, pack_root_id);
    mount_parent_id = task_cs2_mount_parent_id_from_request(&self->pending);
    mount_group = (mount_parent_id >> 16) & 0xffff;
    if( pack_root_idx >= 0 && mount_group > 0 && mount_group != self->await_id )
    {
        int32_t mount_idx = UITree_FindByComponentId(tree, mount_parent_id);
        if( mount_idx >= 0 )
            UITree_Reparent(tree, pack_root_idx, mount_idx);
    }

    UITree_LayoutResolve(tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    if( self->host->bridge )
    {
        uint32_t mi;
        for( mi = 0; mi < tree->component_count; mi++ )
        {
            struct UITreeComponent* c = &tree->components[mi];
            if( c->type != UIELEM_RS_MODEL )
                continue;
            if( c->u.rs_model.gamecache_model_id >= 0 )
            {
                int sid = UITreeSceneBridge_EnsureModel(
                    self->host->bridge, c->u.rs_model.gamecache_model_id);
                if( sid >= 0 )
                    c->u.rs_model.gamecache_model_id = sid;
            }
        }
    }
}

static void
task_cs2_plan_pushscript(struct Task_CS2Run* self)
{
    self->await_id = self->pending.u.push_script.script_id;
    assert(self->await_id > 0);
    self->yield_plan = TASK_CS2_YIELD_SCRIPT;
}

static void
task_cs2_plan_enum(struct Task_CS2Run* self)
{
    if( self->pending.kind == CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT )
        self->await_id = self->pending.u.enum_get_output_count.enum_id;
    else
        self->await_id = self->pending.u.enum_lookup.enum_id;
    assert(self->await_id >= 0);
    self->yield_plan = TASK_CS2_YIELD_ENUM;
}

/* DB opcodes yield for either a DBROW (config kind 38) or a table's
 * DBTABLEINDEX (cache table 21); the request's load_kind/load_id say which. */
static void
task_cs2_plan_db(struct Task_CS2Run* self)
{
    self->await_id = self->pending.u.db.load_id;
    if( self->pending.u.db.load_kind == CS2VM_DB_LOAD_ROW )
        self->yield_plan = TASK_CS2_YIELD_DBROW;
    else if( self->pending.u.db.load_kind == CS2VM_DB_LOAD_INDEX )
        self->yield_plan = TASK_CS2_YIELD_DBINDEX;
    else if( self->pending.u.db.load_kind == CS2VM_DB_LOAD_TABLE )
        self->yield_plan = TASK_CS2_YIELD_DBTABLE;
    else
        self->yield_plan = TASK_CS2_YIELD_NONE;
}

/* The world map is one object for the whole cache, so there is no id to wait
 * on — the load task itself is the wait. */
static void
task_cs2_plan_worldmap(struct Task_CS2Run* self)
{
    self->await_id = -1;
    self->yield_plan = TASK_CS2_YIELD_WORLDMAP;
}

static void
task_cs2_plan_mapelement(struct Task_CS2Run* self)
{
    self->await_id = self->pending.u.mec.mec_id;
    self->yield_plan = self->await_id >= 0 ? TASK_CS2_YIELD_MAPELEMENT : TASK_CS2_YIELD_NONE;
}

/* A struct param answer needs the struct *and* the ParamType behind it; either
 * may be the missing one, so the single yield loads both. struct -1 ("no
 * struct", what a missed enum lookup pushes) is not loadable — the ParamType
 * default answers it. */
static void
task_cs2_plan_struct(struct Task_CS2Run* self)
{
    self->await_id = self->pending.u.struct_param.struct_id;
    self->await_id2 = self->pending.u.struct_param.param_id;
    assert(self->await_id >= 0 || self->await_id2 >= 0);
    self->yield_plan = TASK_CS2_YIELD_STRUCT;
}

static void
task_cs2_plan_obj(struct Task_CS2Run* self)
{
    switch( self->pending.kind )
    {
    case CS2VM_HOST_REQUEST_OC_PARAM:
        /* Same pairing as task_cs2_plan_struct: objtype + ParamType. */
        self->await_id = self->pending.u.oc_param.item_id;
        self->await_id2 = self->pending.u.oc_param.param_id;
        break;
    case CS2VM_HOST_REQUEST_OC_NAME:
        self->await_id = self->pending.u.oc_name.item_id;
        break;
    case CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER:
        self->await_id = self->pending.u.oc_unplaceholder.item_id;
        break;
    case CS2VM_HOST_REQUEST_OC_INT_PARAM:
        self->await_id = self->pending.u.oc_int_param.item_id;
        break;
    case CS2VM_HOST_REQUEST_OC_OP:
        self->await_id = self->pending.u.oc_op.item_id;
        break;
    case CS2VM_HOST_REQUEST_OC_IOP:
        self->await_id = self->pending.u.oc_iop.item_id;
        break;
    case CS2VM_HOST_REQUEST_OC_EXAMINE:
        self->await_id = self->pending.u.oc_examine.item_id;
        break;
    default:
        assert(0 && "task_cs2_plan_obj: unexpected kind");
        self->yield_plan = TASK_CS2_YIELD_ABORT;
        return;
    }
    /* item -1 (empty slot) with a missing ParamType is a valid wait. */
    assert(self->await_id >= 0 || self->await_id2 >= 0);
    self->yield_plan = TASK_CS2_YIELD_OBJ;
}

static void
task_cs2_plan_npc_name(struct Task_CS2Run* self)
{
    assert(self->pending.kind == CS2VM_HOST_REQUEST_NC_NAME);
    self->await_id = self->pending.u.nc_name.npc_id;
    if( self->await_id < 0 )
    {
        self->yield_plan = TASK_CS2_YIELD_NONE;
        return;
    }
    self->yield_plan = TASK_CS2_YIELD_NPC;
}

static void
task_cs2_plan_component(struct Task_CS2Run* self)
{
    self->await_id = task_cs2_group_id_from_request(&self->pending);
    assert(self->await_id > 0 && "component yield must carry a valid group id");
    self->yield_plan = TASK_CS2_YIELD_COMPONENT;
}

static void
task_cs2_plan_widget_set_model(struct Task_CS2Run* self)
{
    self->await_id = self->pending.u.widget_set_model.model_id;
    if( self->await_id < 0 )
    {
        self->yield_plan = TASK_CS2_YIELD_NONE;
        return;
    }
    self->yield_plan = TASK_CS2_YIELD_MODEL;
}

static void
task_cs2_plan_widget_set_model_kind(struct Task_CS2Run* self)
{
    int model_id = self->pending.u.widget_set_model_kind.model_id;
    enum CS2VM_ModelKind kind = self->pending.u.widget_set_model_kind.model_kind;

    if( kind == CS2VM_MODEL_KIND_PLAIN )
    {
        if( model_id < 0 )
        {
            self->yield_plan = TASK_CS2_YIELD_NONE;
            return;
        }
        self->await_id = model_id;
        self->yield_plan = TASK_CS2_YIELD_MODEL;
        return;
    }
    if( kind == CS2VM_MODEL_KIND_NPC_HEAD )
    {
        if( model_id < 0 )
        {
            self->yield_plan = TASK_CS2_YIELD_NONE;
            return;
        }
        self->await_id = model_id;
        self->yield_plan = TASK_CS2_YIELD_NPC;
        return;
    }
    if( kind == CS2VM_MODEL_KIND_PLAYER_HEAD || kind == CS2VM_MODEL_KIND_PLAYER_SELF ||
        kind == CS2VM_MODEL_KIND_PLAYER_CHATHEAD )
    {
        /* No appearance compositor yet — do not abort; let the script continue
         * (e.g. IF_SETTEXT for equipment bonuses). clientCode 328 emit handles preview. */
        fprintf(
            stderr,
            "Task_CS2Run: player model kind %d no-op (script %d)\n",
            (int)kind,
            self->script_id);
        self->yield_plan = TASK_CS2_YIELD_NONE;
        return;
    }
    fprintf(
        stderr, "Task_CS2Run: unhandled model kind %d (script %d)\n", (int)kind, self->script_id);
    self->yield_plan = TASK_CS2_YIELD_ABORT;
}

static void
task_cs2_plan_setobject(struct Task_CS2Run* self)
{
    if( self->pending.kind == CS2VM_HOST_REQUEST_IF_SETOBJECT )
    {
        self->yield_obj_id = self->pending.u.if_set_object.obj_id;
        self->yield_obj_count = self->pending.u.if_set_object.count;
    }
    else
    {
        self->yield_obj_id = self->pending.u.cc_set_object.obj_id;
        self->yield_obj_count = self->pending.u.cc_set_object.count;
    }

    /* obj_id <= 0 clears the slot — no load. */
    if( self->yield_obj_id <= 0 )
    {
        self->yield_plan = TASK_CS2_YIELD_NONE;
        return;
    }
    self->await_id = self->yield_obj_id;
    self->yield_plan = TASK_CS2_YIELD_SETOBJECT;
}

static void
task_cs2_plan_setgraphic(struct Task_CS2Run* self)
{
    self->await_id = self->pending.u.cc_set_graphic.graphic_id;
    if( self->await_id < 0 )
    {
        self->yield_plan = TASK_CS2_YIELD_NONE;
        return;
    }
    self->yield_plan = TASK_CS2_YIELD_SPRITE;
}

static void
task_cs2_plan_font(struct Task_CS2Run* self)
{
    if( self->pending.kind == CS2VM_HOST_REQUEST_CC_SETTEXTFONT )
        self->await_id = self->pending.u.cc_set_text_font.font_id;
    else
        self->await_id = self->pending.u.para_height.font_id;
    if( self->await_id < 0 )
    {
        self->yield_plan = TASK_CS2_YIELD_NONE;
        return;
    }
    self->yield_plan = TASK_CS2_YIELD_FONT;
}

static void
task_cs2_plan_yield(struct Task_CS2Run* self)
{
    assert(self);

    self->yield_plan = TASK_CS2_YIELD_NONE;
    self->await_id = -1;
    self->await_id2 = -1;

    switch( self->pending.kind )
    {
    case CS2VM_HOST_REQUEST_PUSHSCRIPT:
        task_cs2_plan_pushscript(self);
        break;

    case CS2VM_HOST_REQUEST_ENUM_LOOKUP:
    case CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT:
        task_cs2_plan_enum(self);
        break;

    case CS2VM_HOST_REQUEST_STRUCT_PARAM:
        task_cs2_plan_struct(self);
        break;

    /* A component param that misses falls through to the ParamType default, so
     * only the ParamType half of the struct yield is wanted here (await_id -1
     * skips the struct load). */
    case CS2VM_HOST_REQUEST_CC_GETCOMPONENTPARAM:
        self->await_id = -1;
        self->await_id2 = self->pending.u.cc_component_param.param_id;
        self->yield_plan =
            self->await_id2 >= 0 ? TASK_CS2_YIELD_STRUCT : TASK_CS2_YIELD_NONE;
        break;

    case CS2VM_HOST_REQUEST_DB:
        task_cs2_plan_db(self);
        break;

    case CS2VM_HOST_REQUEST_WORLDMAP:
        task_cs2_plan_worldmap(self);
        break;

    case CS2VM_HOST_REQUEST_MEC:
        task_cs2_plan_mapelement(self);
        break;

    /* OC_FIND yields once to bulk-load the whole obj group before its item-name
     * scan; there is no single id to wait on, so the load task is the wait. */
    case CS2VM_HOST_REQUEST_OC_FIND:
        self->await_id = -1;
        self->yield_plan = TASK_CS2_YIELD_OBJALL;
        break;

    case CS2VM_HOST_REQUEST_OC_PARAM:
    case CS2VM_HOST_REQUEST_OC_NAME:
    case CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER:
    case CS2VM_HOST_REQUEST_OC_INT_PARAM:
    case CS2VM_HOST_REQUEST_OC_OP:
    case CS2VM_HOST_REQUEST_OC_IOP:
    case CS2VM_HOST_REQUEST_OC_EXAMINE:
        task_cs2_plan_obj(self);
        break;

    case CS2VM_HOST_REQUEST_NC_NAME:
        task_cs2_plan_npc_name(self);
        break;

    case CS2VM_HOST_REQUEST_CC_CREATE:
    case CS2VM_HOST_REQUEST_CC_FIND:
    case CS2VM_HOST_REQUEST_IF_FIND:
    case CS2VM_HOST_REQUEST_CC_CHILDREN_FIND:
    case CS2VM_HOST_REQUEST_IF_CHILDREN_FIND:
    /* Set-ops yield only when their target's group isn't baked yet (e.g.
     * search targeting the chatbox group); load + mount it like a find. */
    case CS2VM_HOST_REQUEST_WIDGET_SET_INT:
        task_cs2_plan_component(self);
        break;

    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL:
        task_cs2_plan_widget_set_model(self);
        break;

    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND:
        task_cs2_plan_widget_set_model_kind(self);
        break;

    case CS2VM_HOST_REQUEST_CC_SETOBJECT:
    case CS2VM_HOST_REQUEST_IF_SETOBJECT:
        task_cs2_plan_setobject(self);
        break;

    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC:
    case CS2VM_HOST_REQUEST_IF_SETGRAPHIC:
        task_cs2_plan_setgraphic(self);
        break;

    case CS2VM_HOST_REQUEST_PARAHEIGHT:
    case CS2VM_HOST_REQUEST_PARAWIDTH:
    case CS2VM_HOST_REQUEST_CC_SETTEXTFONT:
        task_cs2_plan_font(self);
        break;

    /* Sync-only: host never yields these for cache loads. Leave YIELD_NONE. */
    case CS2VM_HOST_REQUEST_INVS_GET_SIZE:
    case CS2VM_HOST_REQUEST_INVS_GET_OBJ:
    case CS2VM_HOST_REQUEST_INVS_GET_NUM:
    case CS2VM_HOST_REQUEST_INVS_GET_TOTAL:
    case CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR:
    case CS2VM_HOST_REQUEST_VARS_READ_VARBIT:
    case CS2VM_HOST_REQUEST_VARS_READ_VARC_INT:
    case CS2VM_HOST_REQUEST_KEYHELD:
    case CS2VM_HOST_REQUEST_KEYPRESSED:
    case CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING:
    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_INT:
    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_STRING:
    case CS2VM_HOST_REQUEST_CC_DELETEALL:
    case CS2VM_HOST_REQUEST_CC_SETPOSITION:
    case CS2VM_HOST_REQUEST_CC_SETSIZE:
    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC2:
    case CS2VM_HOST_REQUEST_CC_SETTILING:
    case CS2VM_HOST_REQUEST_CC_SETOUTLINE:
    case CS2VM_HOST_REQUEST_CC_SETGRAPHICSHADOW:
    case CS2VM_HOST_REQUEST_CC_SETCOLOUR:
    case CS2VM_HOST_REQUEST_CC_SETFILL:
    case CS2VM_HOST_REQUEST_CC_SETTRANS:
    case CS2VM_HOST_REQUEST_CC_SETNOCLICKTHROUGH:
    case CS2VM_HOST_REQUEST_CC_SETTEXT:
    case CS2VM_HOST_REQUEST_CC_SETTEXTALIGN:
    case CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW:
    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLE:
    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLEBEHAVIOR:
    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADZONE:
    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADTIME:
    case CS2VM_HOST_REQUEST_CC_SETOP:
    case CS2VM_HOST_REQUEST_CC_GETID:
    case CS2VM_HOST_REQUEST_CC_GETX:
    case CS2VM_HOST_REQUEST_CC_GETY:
    case CS2VM_HOST_REQUEST_CC_GETWIDTH:
    case CS2VM_HOST_REQUEST_CC_GETHEIGHT:
    case CS2VM_HOST_REQUEST_CC_GETHIDE:
    case CS2VM_HOST_REQUEST_CC_GETTEXT:
    case CS2VM_HOST_REQUEST_CC_GETCOLOUR:
    case CS2VM_HOST_REQUEST_CC_GETFILLCOLOUR:
    case CS2VM_HOST_REQUEST_CC_GETINVOBJECT:
    case CS2VM_HOST_REQUEST_CC_GETINVCOUNT:
    case CS2VM_HOST_REQUEST_CC_SETCOMPONENTPARAM:
    case CS2VM_HOST_REQUEST_CC_GETTRANS:
    case CS2VM_HOST_REQUEST_CC_SETONCLICK:
    case CS2VM_HOST_REQUEST_CC_SETONHOLD:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE:
    case CS2VM_HOST_REQUEST_CC_SETONDRAG:
    case CS2VM_HOST_REQUEST_CC_SETONSCROLLWHEEL:
    case CS2VM_HOST_REQUEST_CC_SETONKEY:
    case CS2VM_HOST_REQUEST_CC_SETONOP:
    case CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE:
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEREPEAT:
    case CS2VM_HOST_REQUEST_CC_SETONTIMER:
    case CS2VM_HOST_REQUEST_CC_SETONVARTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETONINVTRANSMIT:
    case CS2VM_HOST_REQUEST_CC_SETON_DISCARD:
    case CS2VM_HOST_REQUEST_CC_SETSCROLLPOS:
    case CS2VM_HOST_REQUEST_CC_SETSCROLLSIZE:
    case CS2VM_HOST_REQUEST_CC_FINDROOT:
    case CS2VM_HOST_REQUEST_CC_RESOLVE_PARENT:
    case CS2VM_HOST_REQUEST_IF_GETWIDTH:
    case CS2VM_HOST_REQUEST_IF_GETHEIGHT:
    case CS2VM_HOST_REQUEST_IF_GETY:
    case CS2VM_HOST_REQUEST_IF_GETLAYER:
    case CS2VM_HOST_REQUEST_IF_GETTOP:
    case CS2VM_HOST_REQUEST_IF_GETSCROLLX:
    case CS2VM_HOST_REQUEST_IF_GETSCROLLY:
    case CS2VM_HOST_REQUEST_IF_GETSCROLLHEIGHT:
    case CS2VM_HOST_REQUEST_IF_GETHIDE:
    case CS2VM_HOST_REQUEST_IF_GETX:
    case CS2VM_HOST_REQUEST_IF_GETTEXT:
    case CS2VM_HOST_REQUEST_IF_GETCOLOUR:
    case CS2VM_HOST_REQUEST_IF_GETFILLCOLOUR:
    case CS2VM_HOST_REQUEST_IF_GETINVOBJECT:
    case CS2VM_HOST_REQUEST_IF_GETINVCOUNT:
    case CS2VM_HOST_REQUEST_IF_GETSCROLLWIDTH:
    case CS2VM_HOST_REQUEST_IF_SETHIDE:
    case CS2VM_HOST_REQUEST_IF_SETPOSITION:
    case CS2VM_HOST_REQUEST_IF_SETSIZE:
    case CS2VM_HOST_REQUEST_IF_SETSCROLLPOS:
    case CS2VM_HOST_REQUEST_IF_SETSCROLLSIZE:
    case CS2VM_HOST_REQUEST_IF_SETTEXT:
    case CS2VM_HOST_REQUEST_IF_SETOUTLINE:
    case CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT:
    case CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT:
    case CS2VM_HOST_REQUEST_IF_SETONOP:
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER:
    case CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE:
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT:
    case CS2VM_HOST_REQUEST_IF_SETONTIMER:
    case CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL:
    case CS2VM_HOST_REQUEST_IF_SETONKEY:
    case CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT:
    case CS2VM_HOST_REQUEST_IF_SETONFRIENDTRANSMIT:
    case CS2VM_HOST_REQUEST_SOCIAL:
    case CS2VM_HOST_REQUEST_CHAT:
    case CS2VM_HOST_REQUEST_MAP_WORLD:
    case CS2VM_HOST_REQUEST_IF_SETON_DISCARD:
    case CS2VM_HOST_REQUEST_IF_SETOP:
    case CS2VM_HOST_REQUEST_IF_SETOPBASE:
    case CS2VM_HOST_REQUEST_IF_SETOPSUBMENU:
    case CS2VM_HOST_REQUEST_IF_SETTARGETPRIORITY:
    case CS2VM_HOST_REQUEST_IF_CLEAROPS:
    case CS2VM_HOST_REQUEST_IF_CLEAROPSUBMENU:
    case CS2VM_HOST_REQUEST_WIDGET_SET_OPKEY:
    case CS2VM_HOST_REQUEST_WIDGET_SET_OPKEY_RATE:
    case CS2VM_HOST_REQUEST_WIDGET_SET_INT2:
    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_ANGLE:
    case CS2VM_HOST_REQUEST_WIDGET_SET_ARC:
    case CS2VM_HOST_REQUEST_WIDGET_INPUT_INT:
    case CS2VM_HOST_REQUEST_CLIENTCLOCK:
    case CS2VM_HOST_REQUEST_MOUSE_GETX:
    case CS2VM_HOST_REQUEST_MOUSE_GETY:
    case CS2VM_HOST_REQUEST_CAM_SETFOLLOWHEIGHT:
    case CS2VM_HOST_REQUEST_CAM_GETFOLLOWHEIGHT:
        break;

    default:
        fprintf(
            stderr,
            "Task_CS2Run: unhandled yield kind %d (script %d)\n",
            (int)self->pending.kind,
            self->script_id);
        self->yield_plan = TASK_CS2_YIELD_ABORT;
        break;
    }
}

static int
Task_CS2Run_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_CS2Run* self = (struct Task_CS2Run*)task;
    struct CS2VM2_Thread* thread = NULL;
    enum CS2VM2_ThreadStatus status;
    struct CS2VM2_ThreadError err;
    int j;

    PT_BEGIN(&self->pt);

    assert(self->host);
    assert(self->host->provider);
    self->provider = self->host->provider;

    if( !self->script && self->script_id <= 0 )
    {
        fprintf(stderr, "Task_CS2Run: no script to run\n");
        PT_EXIT(&self->pt);
    }

    if( !self->script )
    {
        if( !CacheProvider_ClientScriptHas(self->provider, self->script_id) )
        {
            self->await_id = self->script_id;
            TASK_AWAITSELF_IF(CreateTask_ClientScriptLoad(self->provider, self->await_id));
        }
        self->script = CacheProvider_ClientScriptGet(self->provider, self->script_id);
        if( !self->script )
        {
            fprintf(stderr, "Task_CS2Run: failed to resolve script %d\n", self->script_id);
            PT_EXIT(&self->pt);
        }
    }

    if( !self->started )
    {
        self->vm = CS2VM2_Acquire();
        assert(self->vm);
        CS2VM2_BindHost(self->vm, self->host, RS_CS2Host_Exec);
        thread = CS2VM2_ThreadMain(self->vm);
        CS2VM2_ThreadSetCanvas(
            thread,
            self->host->viewport_w > 0 ? self->host->viewport_w : 765,
            self->host->viewport_h > 0 ? self->host->viewport_h : 503);
        CS2VM2_ThreadSetWindowMode(
            thread, self->host->window_mode, self->host->default_window_mode);
        CS2VM2_ThreadStart(thread, self->script);

        {
            /* Mixed positional args: ints fill int locals in order, strings
             * fill string locals in order (OSRS ScriptEvent semantics). */
            int int_i = 0;
            int str_i = 0;
            for( j = 0; j < self->int_arg_count; j++ )
            {
                if( j < 64 && (self->str_mask & ((uint64_t)1 << j)) )
                {
                    char const* s =
                        str_i < self->str_arg_count ? self->str_args[str_i] : "";
                    /*
                     * `event_opbase` is the one event local that is a STRING,
                     * so it travels in the arg list as its own literal name
                     * rather than as one of the CS2VM_SCRIPT_ARG_* int
                     * sentinels. Substituting it is the same mechanism
                     * task_cs2_set_int_local applies to the int ones.
                     *
                     * The friends and ignore rows are what surfaced it: each
                     * row does cc_setopbase("<col=ff9040><name></col>") and
                     * then cc_setonop("script126(event_opindex, event_opbase,
                     * ...)"), and script 126 opens the private-message prompt
                     * on removetags(that) — the removetags is there precisely
                     * because the value arrives with the colour tags on. Left
                     * unsubstituted, "Message" on a friend addressed a player
                     * literally called `event_opbase`.
                     */
                    if( strcmp(s, "event_opbase") == 0 )
                    {
                        int32_t opb = UITree_FindByComponentId(
                            self->host->tree, self->active_component_id);
                        if( opb >= 0 )
                            s = self->host->tree->components[opb].menu_options.option;
                    }
                    (void)CS2VM2_SetStringCurrentFrameLocal(thread, str_i, s);
                    str_i++;
                }
                else
                {
                    task_cs2_set_int_local(self, thread, int_i, self->int_args[j]);
                    int_i++;
                }
            }
        }

        CS2VM2_SetActiveAndDotComponentId(thread, self->active_component_id);
        if( self->dot_component_id != self->active_component_id )
            thread->dot_component_id = self->dot_component_id;

        self->started = 1;

        /* TORIRS_CS2_DUMP_SCRIPT=<id>: one-shot disassembly of a script when it
         * starts, to understand hooks that run without failing (e.g. tab-visibility
         * onSubChange). */
        {
            static int s_dumped = 0;
            char const* want = getenv("TORIRS_CS2_DUMP_SCRIPT");
            if( want && !s_dumped )
            {
                struct CS2VM2_Script* d =
                    CacheProvider_ClientScriptGet(self->provider, atoi(want));
                if( d )
                {
                    s_dumped = 1;
                    fprintf(stderr, "== DUMP script %d (ops=%d int_args=%d str_args=%d local_ints=%d) ==\n",
                        d->script_id, d->op_count, d->int_argument_count, d->string_argument_count,
                        d->local_int_count);
                    for( int p = 0; p < d->op_count; p++ )
                        fprintf(
                            stderr,
                            "  pc=%d op=%d %s operand=%d str=%s\n",
                            p, d->opcodes[p], CS2_OpCode_String(d->opcodes[p]),
                            d->int_operands ? d->int_operands[p] : 0,
                            (d->string_operands && d->string_operands[p]) ? d->string_operands[p]
                                                                          : "(null)");
                }
            }
        }
    }

    for( ;; )
    {
        thread = CS2VM2_ThreadMain(self->vm);
        status = CS2VM2_ThreadRun(thread, &err);
        if( status == CS2VM2_THREAD_DONE || status == 0 )
            break;
        if( status == CS2VM2_THREAD_ERROR )
        {
            fprintf(
                stderr,
                "Task_CS2Run: script %d failed at opcode %d pc %d "
                "(invoked as script %d for component 0x%x)\n",
                thread->last_error_script_id,
                thread->last_error_opcode,
                thread->last_error_pc,
                self->script_id,
                (unsigned)self->active_component_id);
            /* Bytecode window around the failing pc — unknown/mis-stubbed
             * opcodes only surface as underflows a few ops later. */
            {
                struct CS2VM2_Script* dbg =
                    CacheProvider_ClientScriptGet(self->provider, thread->last_error_script_id);
                if( dbg )
                {
                    int pci;
                    fprintf(
                        stderr,
                        "  script %d: int_args=%d str_args=%d int_locals=%d str_locals=%d "
                        "ops=%d\n",
                        dbg->script_id,
                        dbg->int_argument_count,
                        dbg->string_argument_count,
                        dbg->local_int_count,
                        dbg->local_string_count,
                        dbg->op_count);
                    /* TORIRS_CS2_DUMP_FULL=1 prints the whole script (to map every
                     * mis-stubbed host op that leaks the stack), else a window. */
                    int dbg_full = getenv("TORIRS_CS2_DUMP_FULL") != NULL;
                    int pc_hi = dbg_full ? dbg->op_count - 1 : thread->last_error_pc + 2;
                    pci = dbg_full ? 0 : thread->last_error_pc - 24;
                    if( pci < 0 )
                        pci = 0;
                    for( ; pci < dbg->op_count && pci <= pc_hi; pci++ )
                        fprintf(
                            stderr,
                            "  pc=%d op=%d %s operand=%d str=%s\n",
                            pci,
                            dbg->opcodes[pci],
                            CS2_OpCode_String(dbg->opcodes[pci]),
                            dbg->int_operands ? dbg->int_operands[pci] : 0,
                            (dbg->string_operands && dbg->string_operands[pci])
                                ? dbg->string_operands[pci]
                                : "(null)");
                }
            }
            CS2VM2_ResetRuntime(thread);
            break;
        }
        if( status != CS2VM2_THREAD_YIELDED )
            break;
        if( !self->host->has_pending )
        {
            fprintf(stderr, "Task_CS2Run: yield without pending host request\n");
            CS2VM2_ResetRuntime(thread);
            break;
        }

        self->pending = self->host->pending;
        self->host->has_pending = false;

        /* Flat switch (no PT) then linear awaits — protothreads cannot nest switch. */
        task_cs2_plan_yield(self);

        if( self->yield_plan == TASK_CS2_YIELD_ABORT )
        {
            CS2VM2_ResetRuntime(CS2VM2_ThreadMain(self->vm));
            PT_EXIT(&self->pt);
        }
        else if( self->yield_plan == TASK_CS2_YIELD_SCRIPT )
        {
            TASK_AWAITSELF_IF(CreateTask_ClientScriptLoad(self->provider, self->await_id));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_ENUM )
        {
            TASK_AWAITSELF_IF(CreateTask_EnumLoad(self->provider, self->await_id));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_WORLDMAP )
        {
            TASK_AWAITSELF_IF(CreateTask_WorldMapLoad(self->provider));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_MAPELEMENT )
        {
            TASK_AWAITSELF_IF(CreateTask_MapElementLoad(self->provider, self->await_id));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_OBJALL )
        {
            TASK_AWAITSELF_IF(CreateTask_ObjLoadAll(self->provider));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_STRUCT )
        {
            if( self->await_id >= 0 )
                TASK_AWAITSELF_IF(CreateTask_StructLoad(self->provider, self->await_id));
            if( self->await_id2 >= 0 )
                TASK_AWAITSELF_IF(CreateTask_ParamLoad(self->provider, self->await_id2));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_DBROW )
        {
            TASK_AWAITSELF_IF(CreateTask_DbRowLoad(self->provider, self->await_id));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_DBINDEX )
        {
            TASK_AWAITSELF_IF(CreateTask_DbTableIndexLoad(self->provider, self->await_id));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_DBTABLE )
        {
            TASK_AWAITSELF_IF(CreateTask_DbTableLoad(self->provider, self->await_id));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_OBJ )
        {
            if( self->await_id >= 0 )
                TASK_AWAITSELF_IF(CreateTask_ObjLoad(self->provider, self->await_id));
            if( self->await_id2 >= 0 )
                TASK_AWAITSELF_IF(CreateTask_ParamLoad(self->provider, self->await_id2));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_COMPONENT )
        {
            assert(self->host->tree);
            TASK_AWAITSELF_IF(CreateTask_ComponentPackLoad(self->provider, self->await_id));
            TASK_AWAITSELF_IF(CreateTask_PackAssetsLoad(self->provider, self->await_id));
            task_cs2_bake_pack(self);
            if( !CacheProvider_ComponentPackHas(self->provider, self->await_id) )
            {
                CS2VM2_ResetRuntime(CS2VM2_ThreadMain(self->vm));
                PT_EXIT(&self->pt);
            }
        }
        else if( self->yield_plan == TASK_CS2_YIELD_MODEL )
        {
            TASK_AWAITSELF_IF(CreateTask_ModelLoad(self->provider, self->await_id));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_NPC )
        {
            TASK_AWAITSELF_IF(CreateTask_NpcLoad(self->provider, self->await_id));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_SETOBJECT )
        {
            int ids[1];
            int counts[1];
            ids[0] = self->yield_obj_id;
            counts[0] = self->yield_obj_count;
            TASK_AWAITSELF_IF(CreateTask_ObjModelLoad(self->provider, ids, counts, 1));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_SPRITE )
        {
            TASK_AWAITSELF_IF(CreateTask_SpriteLoad(self->provider, self->await_id));
        }
        else if( self->yield_plan == TASK_CS2_YIELD_FONT )
        {
            TASK_AWAITSELF_IF(CreateTask_FontLoad(self->provider, self->await_id));
        }
        /* TASK_CS2_YIELD_NONE: expected no-op (clear ids). Re-enter ThreadRun. */
    }

    PT_END(&self->pt);
    return 0;
}

static void
Task_CS2Run_Free(struct ToriRS_Task* task)
{
    struct Task_CS2Run* self = (struct Task_CS2Run*)task;
    assert(self);
    /* NULL when the task was dropped before it ever ran (queue teardown). */
    CS2VM2_Release(self->vm);
    free(self);
}

static struct ToriRS_TaskVTable Task_CS2Run_VTable = {
    .run = Task_CS2Run_Run,
    .free = Task_CS2Run_Free,
};

static struct ToriRS_Task*
task_cs2_run_new(
    struct RS_CS2Host* host,
    int script_id,
    struct CS2VM2_Script* script,
    int active_component_id,
    int dot_component_id,
    int const* int_args,
    int int_arg_count,
    uint64_t str_mask,
    char const* const* str_args,
    int str_arg_count)
{
    struct Task_CS2Run* self;

    assert(host);
    assert(host->provider);

    self = calloc(1, sizeof(*self));
    assert(self);

    self->task.vtable = &Task_CS2Run_VTable;
    strcpy(self->task.name, "CS2Run");
    self->host = host;
    self->provider = host->provider;
    self->script_id = script_id;
    self->script = script;
    self->active_component_id = active_component_id;
    self->dot_component_id = dot_component_id >= 0 ? dot_component_id : active_component_id;

    /* Freeze the event the dispatcher just wrote. See struct Task_CS2Run. */
    self->event_key_typed = host->event_key_typed;
    self->event_key_pressed = host->event_key_pressed;
    self->event_mouse_x = host->event_mouse_x;
    self->event_mouse_y = host->event_mouse_y;
    self->event_op_index = host->event_op_index;
    self->event_op_subindex = host->event_op_subindex;
    self->event_drag_target_id = host->event_drag_target_id;
    self->event_drag_target_child_index = host->event_drag_target_child_index;

    if( int_arg_count > TASK_CS2_RUN_INT_ARGS_MAX )
        int_arg_count = TASK_CS2_RUN_INT_ARGS_MAX;
    if( int_arg_count < 0 )
        int_arg_count = 0;
    self->int_arg_count = int_arg_count;
    if( int_arg_count > 0 && int_args )
        memcpy(self->int_args, int_args, (size_t)int_arg_count * sizeof(int));

    self->str_mask = str_mask;
    if( str_arg_count > TASK_CS2_RUN_STR_ARGS_MAX )
        str_arg_count = TASK_CS2_RUN_STR_ARGS_MAX;
    if( str_arg_count < 0 || !str_args )
        str_arg_count = 0;
    self->str_arg_count = str_arg_count;
    for( int i = 0; i < str_arg_count; i++ )
    {
        strncpy(self->str_args[i], str_args[i] ? str_args[i] : "", TASK_CS2_RUN_STR_ARG_LEN - 1);
        self->str_args[i][TASK_CS2_RUN_STR_ARG_LEN - 1] = '\0';
    }

    PT_INIT(&self->pt);
    /* Display dropdown / server remount: settings_client_mode (script_3998) may
     * early-out on setwindowmode when already in the same fixed/resizable class
     * (Classic↔Modern). Stash the mode arg at dispatch so WINDOW_STATUS still
     * fires. */
    if( host->script_settings_client_mode > 0 &&
        script_id == host->script_settings_client_mode && int_arg_count >= 1 && int_args )
    {
        int layout = int_args[0];
        if( layout >= 0 && layout <= 2 )
        {
            host->client_layout_mode = layout;
            host->client_layout_dirty = true;
        }
    }
    if( getenv("TORIRS_CC_DEBUG") )
        fprintf(
            stderr,
            "CS2RUN script=%d com=%d|%d\n",
            script_id,
            (active_component_id >> 16) & 0xffff,
            active_component_id & 0xffff);
    return &self->task;
}

struct ToriRS_Task*
CreateTask_CS2Run(
    struct RS_CS2Host* host,
    int script_id,
    int active_component_id,
    int dot_component_id,
    int const* int_args,
    int int_arg_count)
{
    return task_cs2_run_new(
        host,
        script_id,
        NULL,
        active_component_id,
        dot_component_id,
        int_args,
        int_arg_count,
        0,
        NULL,
        0);
}

struct ToriRS_Task*
CreateTask_CS2RunMixed(
    struct RS_CS2Host* host,
    int script_id,
    int active_component_id,
    int dot_component_id,
    int const* args,
    int arg_count,
    uint64_t str_mask,
    char const* const* str_args,
    int str_arg_count)
{
    return task_cs2_run_new(
        host,
        script_id,
        NULL,
        active_component_id,
        dot_component_id,
        args,
        arg_count,
        str_mask,
        str_args,
        str_arg_count);
}

struct ToriRS_Task*
CreateTask_CS2RunScript(
    struct RS_CS2Host* host,
    struct CS2VM2_Script* script,
    int active_component_id,
    int dot_component_id,
    int const* int_args,
    int int_arg_count)
{
    assert(script);
    return task_cs2_run_new(
        host,
        script->script_id,
        script,
        active_component_id,
        dot_component_id,
        int_args,
        int_arg_count,
        0,
        NULL,
        0);
}

/* =========================================================================
 * Inv-transmit dispatch
 * ========================================================================= */

struct Task_CS2InvTransmitDispatch
{
    struct ToriRS_Task task;
    struct pt pt;

    struct RS_CS2Host* host;
    int container_id;
    int hook_index;
};

static int
hook_matches_container(
    struct RS_CS2InvTransmitHook const* hook,
    int container_id)
{
    int i;
    assert(hook);
    if( container_id < 0 )
        return 1;
    if( hook->trigger_count <= 0 )
        return 1;
    for( i = 0; i < hook->trigger_count; i++ )
    {
        if( hook->trigger_ids[i] == container_id )
            return 1;
    }
    return 0;
}

static int
Task_CS2InvTransmitDispatch_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_CS2InvTransmitDispatch* self = (struct Task_CS2InvTransmitDispatch*)task;
    struct RS_CS2InvTransmitHook* hook;

    PT_BEGIN(&self->pt);

    assert(self->host);

    for( self->hook_index = 0; self->hook_index < self->host->inv_transmit_hook_count;
         self->hook_index++ )
    {
        hook = &self->host->inv_transmit_hooks[self->hook_index];
        if( !hook_matches_container(hook, self->container_id) )
            continue;
        if( hook->script_id <= 0 )
            continue;
        /* Dead hook (component reclaimed): mark seen so it never fires — a missing
         * component reads as "not hidden" below. */
        if( UITree_FindByComponentId(self->host->tree, hook->component_id) < 0 )
        {
            hook->last_seen_serial = self->host->inv_change_serial;
            continue;
        }
        /* Hidden: skip WITHOUT advancing so the hook fires once when unhidden
         * (TS parity: hidden nodes are skipped before counter sync). */
        if( UITree_ComponentOrAncestorHidden(self->host->tree, hook->component_id) )
            continue;
        /* Already fired for the current inv state — the per-hook serial gate that
         * makes widgets-loaded re-traversals free (TS lastChangedInvCount). */
        if( hook->last_seen_serial >= self->host->inv_change_serial )
            continue;
        hook->last_seen_serial = self->host->inv_change_serial;

#if UITREE_CLICK_DEBUG
        fprintf(
            stderr,
            "uitree_click: InvTransmitDispatch script_id=%d component_id=%d argc=%d "
            "container_filter=%d\n",
            hook->script_id,
            hook->component_id,
            hook->int_arg_count,
            self->container_id);
#endif

        {
            char const* str_ptrs[CS2VM_SETON_STR_ARG_MAX];
            int si;
            for( si = 0; si < CS2VM_SETON_STR_ARG_MAX; si++ )
                str_ptrs[si] = hook->str_args[si];
            /* CreateTask copies the strings immediately, so the locals need
             * not survive the protothread yield. */
            TASK_AWAITSELF(CreateTask_CS2RunMixed(
                self->host,
                hook->script_id,
                hook->component_id,
                hook->component_id,
                hook->int_args,
                hook->int_arg_count,
                hook->str_arg_mask,
                str_ptrs,
                hook->str_arg_count));
        }
    }

    PT_END(&self->pt);
    return 0;
}

static void
Task_CS2InvTransmitDispatch_Free(struct ToriRS_Task* task)
{
    struct Task_CS2InvTransmitDispatch* self = (struct Task_CS2InvTransmitDispatch*)task;
    assert(self);
    free(self);
}

static struct ToriRS_TaskVTable Task_CS2InvTransmitDispatch_VTable = {
    .run = Task_CS2InvTransmitDispatch_Run,
    .free = Task_CS2InvTransmitDispatch_Free,
};

struct ToriRS_Task*
CreateTask_CS2InvTransmitDispatch(
    struct RS_CS2Host* host,
    int container_id)
{
    struct Task_CS2InvTransmitDispatch* self;

    assert(host);

    self = calloc(1, sizeof(*self));
    assert(self);
    self->task.vtable = &Task_CS2InvTransmitDispatch_VTable;
    strcpy(self->task.name, "CS2InvTransmitDispatch");
    self->host = host;
    self->container_id = container_id;
    PT_INIT(&self->pt);
    return &self->task;
}

/* =========================================================================
 * Var-transmit dispatch
 * ========================================================================= */

struct Task_CS2VarTransmitDispatch
{
    struct ToriRS_Task task;
    struct pt pt;

    struct RS_CS2Host* host;
    /** Snapshot of the changed-var set this dispatch is for (the host's set is
     *  cleared and refilled while this task walks the hooks across yields).
     *  var_count == 0 means "every hook", the pre-filter behavior. */
    int var_ids[RS_CS2_HOST_VAR_CHANGED_MAX];
    int var_count;
    int hook_index;
};

/** Does `var_id` name the varbit whose base varp is `changed_id`? A hook may
 *  list varbit triggers while the change notification carries the base varp
 *  (VarPManager_SetVarbitOptimistic writes through the base), so matching on the
 *  raw id alone would silently stop such a hook from ever re-running. */
static int
var_trigger_is_varbit_of(
    struct RS_CS2Host const* host,
    int trigger_id,
    int changed_id)
{
    struct VarPManager const* varps = host->varps;
    if( !varps || trigger_id < 0 || trigger_id >= varps->varbit_count )
        return 0;
    return varps->varbit_types[trigger_id].basevar == changed_id;
}

static int
hook_matches_var(
    struct RS_CS2Host const* host,
    struct RS_CS2VarTransmitHook const* hook,
    int const* var_ids,
    int var_count)
{
    assert(hook);
    if( var_count <= 0 )
        return 1;
    /* No trigger list = "any change" (that is what the wildcard dispatch did for
     * these hooks, and a hook with no triggers has nothing to filter on). */
    if( hook->trigger_count <= 0 )
        return 1;
    for( int i = 0; i < hook->trigger_count; i++ )
    {
        for( int v = 0; v < var_count; v++ )
        {
            if( hook->trigger_ids[i] == var_ids[v] ||
                var_trigger_is_varbit_of(host, hook->trigger_ids[i], var_ids[v]) )
                return 1;
        }
    }
    return 0;
}

static int
Task_CS2VarTransmitDispatch_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_CS2VarTransmitDispatch* self = (struct Task_CS2VarTransmitDispatch*)task;
    struct RS_CS2VarTransmitHook* hook;

    PT_BEGIN(&self->pt);

    assert(self->host);

    for( self->hook_index = 0; self->hook_index < self->host->var_transmit_hook_count;
         self->hook_index++ )
    {
        hook = &self->host->var_transmit_hooks[self->hook_index];
        if( !hook_matches_var(self->host, hook, self->var_ids, self->var_count) )
            continue;
        if( hook->script_id <= 0 )
            continue;
        /* Dead hook (component reclaimed): mark seen so it never fires — a missing
         * component reads as "not hidden" below. */
        if( UITree_FindByComponentId(self->host->tree, hook->component_id) < 0 )
        {
            hook->last_seen_serial = self->host->var_change_serial;
            continue;
        }
        /* Hidden: skip WITHOUT advancing so the hook fires once when unhidden. */
        if( UITree_ComponentOrAncestorHidden(self->host->tree, hook->component_id) )
            continue;
        /* Already fired for the current var state (TS lastChangedVarpCount gate). */
        if( hook->last_seen_serial >= self->host->var_change_serial )
            continue;
        hook->last_seen_serial = self->host->var_change_serial;

        {
            char const* str_ptrs[CS2VM_SETON_STR_ARG_MAX];
            int si;
            for( si = 0; si < CS2VM_SETON_STR_ARG_MAX; si++ )
                str_ptrs[si] = hook->str_args[si];
            /* CreateTask copies the strings immediately, so the locals need
             * not survive the protothread yield. */
            TASK_AWAITSELF(CreateTask_CS2RunMixed(
                self->host,
                hook->script_id,
                hook->component_id,
                hook->component_id,
                hook->int_args,
                hook->int_arg_count,
                hook->str_arg_mask,
                str_ptrs,
                hook->str_arg_count));
        }
    }

    PT_END(&self->pt);
    return 0;
}

static void
Task_CS2VarTransmitDispatch_Free(struct ToriRS_Task* task)
{
    struct Task_CS2VarTransmitDispatch* self = (struct Task_CS2VarTransmitDispatch*)task;
    assert(self);
    free(self);
}

static struct ToriRS_TaskVTable Task_CS2VarTransmitDispatch_VTable = {
    .run = Task_CS2VarTransmitDispatch_Run,
    .free = Task_CS2VarTransmitDispatch_Free,
};

struct ToriRS_Task*
CreateTask_CS2VarTransmitDispatchSet(
    struct RS_CS2Host* host,
    int const* var_ids,
    int var_count)
{
    struct Task_CS2VarTransmitDispatch* self;

    assert(host);

    self = calloc(1, sizeof(*self));
    assert(self);
    self->task.vtable = &Task_CS2VarTransmitDispatch_VTable;
    strcpy(self->task.name, "CS2VarTransmitDispatch");
    self->host = host;
    if( var_ids && var_count > 0 )
    {
        if( var_count > RS_CS2_HOST_VAR_CHANGED_MAX )
            var_count = RS_CS2_HOST_VAR_CHANGED_MAX;
        memcpy(self->var_ids, var_ids, (size_t)var_count * sizeof(int));
        self->var_count = var_count;
    }
    PT_INIT(&self->pt);
    return &self->task;
}

struct ToriRS_Task*
CreateTask_CS2VarTransmitDispatch(
    struct RS_CS2Host* host,
    int var_id)
{
    if( var_id < 0 )
        return CreateTask_CS2VarTransmitDispatchSet(host, NULL, 0);
    return CreateTask_CS2VarTransmitDispatchSet(host, &var_id, 1);
}

/* =========================================================================
 * Stat-transmit dispatch
 * =========================================================================
 *
 * The skill half of the same reactive loop the var and inv dispatches drive.
 * `RS_CS2Host_NotifyStatChanged` (called by UPDATE_STAT) bumps
 * `stat_change_serial`; this fires every registered hook whose trigger list
 * names one of the changed skills.
 *
 * The XP-drop panel is what needs it. Its listener is registered with all
 * twenty-four skill ids as triggers, and the script diffs the experience values
 * it is handed against the ones it kept from last time — so a hook that fires
 * for the wrong reason is not harmless, it is a spurious drop.
 *
 * Structurally identical to the var dispatch, including the two gates that look
 * like they could be dropped and cannot: a hidden component is skipped WITHOUT
 * advancing its serial (so it fires once when unhidden), and a reclaimed one has
 * its serial advanced (so it never fires at all).
 */

struct Task_CS2StatTransmitDispatch
{
    struct ToriRS_Task task;
    struct pt pt;

    struct RS_CS2Host* host;
    int stat_ids[RS_CS2_HOST_VAR_CHANGED_MAX];
    int stat_count;
    int hook_index;
    /* How many hooks existed when this dispatch started — see the loop. */
    int hook_count;
};

static int
hook_matches_stat(
    struct RS_CS2StatTransmitHook const* hook,
    int const* stat_ids,
    int stat_count)
{
    assert(hook);
    if( stat_count <= 0 )
        return 1;
    /* No trigger list = "any change", the same convention the var hooks use. */
    if( hook->trigger_count <= 0 )
        return 1;
    for( int i = 0; i < hook->trigger_count; i++ )
    {
        for( int v = 0; v < stat_count; v++ )
        {
            if( hook->trigger_ids[i] == stat_ids[v] )
                return 1;
        }
    }
    return 0;
}

static int
Task_CS2StatTransmitDispatch_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_CS2StatTransmitDispatch* self = (struct Task_CS2StatTransmitDispatch*)task;
    struct RS_CS2StatTransmitHook* hook;

    PT_BEGIN(&self->pt);

    assert(self->host);

    /*
     * The bound is snapshotted, and that is not a micro-optimisation.
     *
     * A dispatched hook runs a clientscript, and the XP-drop script's own job
     * includes re-arming its listener (`xpdrops_setstatlistener`). Re-arming an
     * existing component reuses its slot, but re-arming a *different* one
     * appends — so reading `stat_transmit_hook_count` fresh each iteration lets
     * a hook extend the loop it is being run from. It does, and the client
     * hangs: no crash, no error, just a frame that never completes.
     *
     * Anything registered during this dispatch belongs to the next one. That is
     * also the semantics you want — a listener armed by a stat change has not
     * missed the change that armed it, because the script that armed it just
     * ran for it.
     */
    self->hook_count = self->host->stat_transmit_hook_count;

    for( self->hook_index = 0; self->hook_index < self->hook_count; self->hook_index++ )
    {
        if( self->hook_index >= self->host->stat_transmit_hook_count )
            break; /* compacted mid-dispatch (a component was reclaimed) */
        hook = &self->host->stat_transmit_hooks[self->hook_index];
        if( !hook_matches_stat(hook, self->stat_ids, self->stat_count) )
            continue;
        if( hook->script_id <= 0 )
            continue;
        if( UITree_FindByComponentId(self->host->tree, hook->component_id) < 0 )
        {
            hook->last_seen_serial = self->host->stat_change_serial;
            continue;
        }
        if( UITree_ComponentOrAncestorHidden(self->host->tree, hook->component_id) )
            continue;
        if( hook->last_seen_serial >= self->host->stat_change_serial )
            continue;
        hook->last_seen_serial = self->host->stat_change_serial;
        if( getenv("TORIRS_STAT_DEBUG") )
            fprintf(stderr, "statdisp: hook %d com=0x%x script=%d serial=%u\n", self->hook_index,
                    hook->component_id, hook->script_id, self->host->stat_change_serial);

        {
            char const* str_ptrs[CS2VM_SETON_STR_ARG_MAX];
            int si;
            for( si = 0; si < CS2VM_SETON_STR_ARG_MAX; si++ )
                str_ptrs[si] = hook->str_args[si];
            TASK_AWAITSELF(CreateTask_CS2RunMixed(
                self->host,
                hook->script_id,
                hook->component_id,
                hook->component_id,
                hook->int_args,
                hook->int_arg_count,
                hook->str_arg_mask,
                str_ptrs,
                hook->str_arg_count));
        }
    }

    PT_END(&self->pt);
    return 0;
}

static void
Task_CS2StatTransmitDispatch_Free(struct ToriRS_Task* task)
{
    free(task);
}

static struct ToriRS_TaskVTable Task_CS2StatTransmitDispatch_VTable = {
    .run = Task_CS2StatTransmitDispatch_Run,
    .free = Task_CS2StatTransmitDispatch_Free,
};

struct ToriRS_Task*
CreateTask_CS2StatTransmitDispatchSet(
    struct RS_CS2Host* host,
    int const* stat_ids,
    int stat_count)
{
    struct Task_CS2StatTransmitDispatch* self;

    assert(host);

    self = calloc(1, sizeof(*self));
    assert(self);
    self->task.vtable = &Task_CS2StatTransmitDispatch_VTable;
    strcpy(self->task.name, "CS2StatTransmitDispatch");
    self->host = host;
    if( stat_ids && stat_count > 0 )
    {
        if( stat_count > RS_CS2_HOST_VAR_CHANGED_MAX )
            stat_count = RS_CS2_HOST_VAR_CHANGED_MAX;
        memcpy(self->stat_ids, stat_ids, (size_t)stat_count * sizeof(int));
        self->stat_count = stat_count;
    }
    PT_INIT(&self->pt);
    return &self->task;
}

/* =========================================================================
 * Sub-change dispatch
 * =========================================================================
 *
 * The "a sub-interface came or went" hooks. Mounting one already runs them
 * (task_interface_open step 8); unmounting did not, which is why closing a side
 * panel left the sidebar blank — the gameframe's hook is what puts the tab strip
 * back, and nothing was asking it to.
 *
 * Snapshotted before the first hook runs, for the same reason the mount path
 * snapshots: a hook can create and delete components, so component indices do
 * not survive the yield.
 */

#define TASK_SUBCHANGE_HOOK_MAX 256

struct Task_CS2SubChangeHook
{
    int component_id;
    int script_id;
    int argc;
    int argv[UITREE_HOOK_ARG_MAX];
    uint64_t str_mask;
    int str_argc;
    char strv[UITREE_HOOK_STR_ARG_MAX][UITREE_HOOK_STR_ARG_LEN];
};

struct Task_CS2SubChangeDispatch
{
    struct ToriRS_Task task;
    struct pt pt;

    struct RS_CS2Host* host;
    struct Task_CS2SubChangeHook hooks[TASK_SUBCHANGE_HOOK_MAX];
    int hook_count;
    int hook_index;
};

static int
Task_CS2SubChangeDispatch_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_CS2SubChangeDispatch* self = (struct Task_CS2SubChangeDispatch*)task;

    PT_BEGIN(&self->pt);

    assert(self->host);

    for( self->hook_index = 0; self->hook_index < self->hook_count; self->hook_index++ )
    {
        struct Task_CS2SubChangeHook const* hook = &self->hooks[self->hook_index];
        char const* strp[UITREE_HOOK_STR_ARG_MAX];

        /* The hook's component may have been reclaimed by an earlier hook in
         * this same pass. */
        if( UITree_FindByComponentId(self->host->tree, hook->component_id) < 0 )
            continue;
        for( int si = 0; si < UITREE_HOOK_STR_ARG_MAX; si++ )
            strp[si] = hook->strv[si];
        TASK_AWAITSELF(CreateTask_CS2RunMixed(
            self->host,
            hook->script_id,
            hook->component_id,
            hook->component_id,
            hook->argc > 0 ? hook->argv : NULL,
            hook->argc,
            hook->str_mask,
            strp,
            hook->str_argc));
    }

    PT_END(&self->pt);
    return 0;
}

static void
Task_CS2SubChangeDispatch_Free(struct ToriRS_Task* task)
{
    free(task);
}

static struct ToriRS_TaskVTable Task_CS2SubChangeDispatch_VTable = {
    .run = Task_CS2SubChangeDispatch_Run,
    .free = Task_CS2SubChangeDispatch_Free,
};

struct ToriRS_Task*
CreateTask_CS2SubChangeDispatch(struct RS_CS2Host* host)
{
    struct Task_CS2SubChangeDispatch* self;

    assert(host);
    self = calloc(1, sizeof(*self));
    assert(self);
    self->task.vtable = &Task_CS2SubChangeDispatch_VTable;
    strcpy(self->task.name, "CS2SubChangeDispatch");
    self->host = host;

    if( host->tree )
    {
        for( uint32_t i = 0;
             i < host->tree->component_count && self->hook_count < TASK_SUBCHANGE_HOOK_MAX;
             i++ )
        {
            struct UITreeComponent const* node = &host->tree->components[i];
            struct UITreeRuntimeScriptHook const* slot = &UITree_Hooks(node)->on_sub_change;
            struct Task_CS2SubChangeHook* dst;

            if( node->freed || slot->script_id <= 0 )
                continue;
            dst = &self->hooks[self->hook_count++];
            dst->component_id = node->component_id;
            dst->script_id = slot->script_id;
            dst->argc = slot->argc > UITREE_HOOK_ARG_MAX ? UITREE_HOOK_ARG_MAX : slot->argc;
            if( dst->argc > 0 )
                memcpy(dst->argv, slot->argv, (size_t)dst->argc * sizeof(int));
            dst->str_mask = slot->str_mask;
            dst->str_argc = slot->str_argc;
            memcpy(dst->strv, slot->strv, sizeof(dst->strv));
        }
    }

    PT_INIT(&self->pt);
    return &self->task;
}

/*
 * Misc-transmit dispatch — the run-energy and run-weight orbs.
 *
 * Structurally identical to the sub-change walker above, reading
 * `runtime_hooks.on_misc_transmit`, and it is a separate task for the same
 * reason that one is: the snapshot has to be taken up front, because running a
 * hook can mutate the tree underneath the walk (a hook that rebuilds its own
 * subtree would otherwise invalidate the iteration mid-pass). The
 * `UITree_FindByComponentId` re-check inside the loop is what covers a
 * component reclaimed by an *earlier* hook in the same pass.
 *
 * Fired at most once per client tick from App_Tick, gated on a serial, because
 * the walk touches every component in the tree.
 */
struct Task_CS2MiscTransmitDispatch
{
    struct ToriRS_Task task;
    struct pt pt;

    struct RS_CS2Host* host;
    struct Task_CS2SubChangeHook hooks[TASK_SUBCHANGE_HOOK_MAX];
    int hook_count;
    int hook_index;
};

static int
Task_CS2MiscTransmitDispatch_Run(
    struct ToriRS_Task* task,
    struct ToriRS_IO* io)
{
    struct Task_CS2MiscTransmitDispatch* self = (struct Task_CS2MiscTransmitDispatch*)task;

    PT_BEGIN(&self->pt);

    assert(self->host);

    for( self->hook_index = 0; self->hook_index < self->hook_count; self->hook_index++ )
    {
        struct Task_CS2SubChangeHook const* hook = &self->hooks[self->hook_index];
        char const* strp[UITREE_HOOK_STR_ARG_MAX];

        if( UITree_FindByComponentId(self->host->tree, hook->component_id) < 0 )
            continue;
        for( int si = 0; si < UITREE_HOOK_STR_ARG_MAX; si++ )
            strp[si] = hook->strv[si];
        TASK_AWAITSELF(CreateTask_CS2RunMixed(
            self->host,
            hook->script_id,
            hook->component_id,
            hook->component_id,
            hook->argc > 0 ? hook->argv : NULL,
            hook->argc,
            hook->str_mask,
            strp,
            hook->str_argc));
    }

    PT_END(&self->pt);
    return 0;
}

static void
Task_CS2MiscTransmitDispatch_Free(struct ToriRS_Task* task)
{
    free(task);
}

static struct ToriRS_TaskVTable Task_CS2MiscTransmitDispatch_VTable = {
    .run = Task_CS2MiscTransmitDispatch_Run,
    .free = Task_CS2MiscTransmitDispatch_Free,
};

/* One walker, two channels: `slot_of` picks which hook field to snapshot. Both
 * channels are "no trigger list, re-run everything", so the only difference
 * between them is that one word. */
static struct ToriRS_Task*
create_no_trigger_transmit_dispatch(
    struct RS_CS2Host* host,
    char const* task_name,
    struct UITreeRuntimeScriptHook const* (*slot_of)(struct UITreeComponent const*))
{
    struct Task_CS2MiscTransmitDispatch* self;

    assert(host);
    assert(slot_of);
    self = calloc(1, sizeof(*self));
    assert(self);
    self->task.vtable = &Task_CS2MiscTransmitDispatch_VTable;
    snprintf(self->task.name, sizeof(self->task.name), "%s", task_name);
    self->host = host;

    if( host->tree )
    {
        for( uint32_t i = 0;
             i < host->tree->component_count && self->hook_count < TASK_SUBCHANGE_HOOK_MAX;
             i++ )
        {
            struct UITreeComponent const* node = &host->tree->components[i];
            struct UITreeRuntimeScriptHook const* slot = slot_of(node);
            struct Task_CS2SubChangeHook* dst;

            if( node->freed || slot->script_id <= 0 )
                continue;
            /* Unmounted packs stay in the tree hidden; their listeners were
             * cleared by ClearHooksForInterfaceGroup, but skip anyway so a
             * missed clear cannot keep firing. */
            if( UITree_ComponentOrAncestorHidden(host->tree, node->component_id) )
                continue;
            dst = &self->hooks[self->hook_count++];
            dst->component_id = node->component_id;
            dst->script_id = slot->script_id;
            dst->argc = slot->argc > UITREE_HOOK_ARG_MAX ? UITREE_HOOK_ARG_MAX : slot->argc;
            if( dst->argc > 0 )
                memcpy(dst->argv, slot->argv, (size_t)dst->argc * sizeof(int));
            dst->str_mask = slot->str_mask;
            dst->str_argc = slot->str_argc;
            memcpy(dst->strv, slot->strv, sizeof(dst->strv));
        }
    }

    PT_INIT(&self->pt);
    return &self->task;
}

static struct UITreeRuntimeScriptHook const*
misc_transmit_slot(struct UITreeComponent const* node)
{
    return &UITree_Hooks(node)->on_misc_transmit;
}

static struct UITreeRuntimeScriptHook const*
friend_transmit_slot(struct UITreeComponent const* node)
{
    return &UITree_Hooks(node)->on_friend_transmit;
}

struct ToriRS_Task*
CreateTask_CS2MiscTransmitDispatch(struct RS_CS2Host* host)
{
    return create_no_trigger_transmit_dispatch(
        host, "CS2MiscTransmitDispatch", misc_transmit_slot);
}

/*
 * Friend-transmit dispatch — the friends (429) and ignore (432) side panels.
 *
 * The same walker as misc for the same reason: IF_SETONFRIENDTRANSMIT carries
 * no trigger list, so a change to the friend store re-runs every registered
 * hook. The hooks re-entered here are scripts 631 and 630, which are one-line
 * forwarders to the list builders 125 and 129 — and those cc_deleteall the row
 * container and rebuild it, which is exactly the "a hook mutates the tree
 * underneath the walk" case the up-front snapshot exists for.
 */
struct ToriRS_Task*
CreateTask_CS2FriendTransmitDispatch(struct RS_CS2Host* host)
{
    return create_no_trigger_transmit_dispatch(
        host, "CS2FriendTransmitDispatch", friend_transmit_slot);
}
