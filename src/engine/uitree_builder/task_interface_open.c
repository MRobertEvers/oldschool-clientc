#include "task_interface_open.h"

#include "engine/cache_provider.h"
#include "engine/task_obj_model_load.h"
#include "engine/torirs_types.h"
#include "engine/uitree_from_component.h"
#include "engine/uitree_scene_bridge.h"
#include "game/rs_cs2_host.h"
#include "game/task_cs2_run.h"
#include "inv/inv_manager.h"
#include "task_pack_assets_load.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"

#include "asyncio.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Standard human idle sequence for the player preview (clientCode 327/328). */
#ifndef INTERFACE_PLAYER_IDLE_SEQ
#define INTERFACE_PLAYER_IDLE_SEQ 808
#endif

#define INTERFACE_OPEN_ONLOAD_ARGV_MAX TORIRS_COMPONENT_HOOK_ARG_MAX
#define INTERFACE_OPEN_ONLOAD_MAX 256
#define INTERFACE_OPEN_RUNTIME_HOOK_MAX 512
#define INTERFACE_OPEN_SEED_OBJ_MAX 32

struct InterfaceOpenOnLoad
{
    int component_id;
    int script_id;
    int argc;
    int argv[INTERFACE_OPEN_ONLOAD_ARGV_MAX];
};

struct InterfaceOpenRuntimeHook
{
    int component_id;
    int script_id;
    int argc;
    int argv[32];
};

struct Task_InterfaceOpen
{
    struct ToriRS_Task task;
    struct pt pt;

    struct CacheProvider* provider;
    struct UITree* tree;
    struct RS_CS2Host* host;
    struct InvManager* invs;
    struct UITreeSceneBridge* bridge;
    int interface_id;
    int target_uid; /* -1 = root open */
    int mount_type;
    struct InterfaceOpenStats* stats;

    struct InterfaceOpenOnLoad onloads[INTERFACE_OPEN_ONLOAD_MAX];
    int onload_count;

    struct InterfaceOpenRuntimeHook runtime_hooks[INTERFACE_OPEN_RUNTIME_HOOK_MAX];
    int runtime_hook_count;

    int seed_obj_ids[INTERFACE_OPEN_SEED_OBJ_MAX];
    int seed_obj_count;

    int i;
    int script_id;
};

static int
open_resolve_sprite(
    void* ud,
    int graphic_id)
{
    struct Task_InterfaceOpen* self = (struct Task_InterfaceOpen*)ud;
    int scene_id;
    assert(self && self->bridge);
    if( graphic_id <= 0 )
        return -1;
    scene_id = UITreeSceneBridge_EnsureSprite(self->bridge, graphic_id);
    if( scene_id < 0 )
        fprintf(stderr, "InterfaceOpen: RS sprite %d not in scene\n", graphic_id);
    return scene_id;
}

static int
open_resolve_font(
    void* ud,
    int font_id)
{
    struct Task_InterfaceOpen* self = (struct Task_InterfaceOpen*)ud;
    assert(self && self->bridge);
    if( font_id < 0 )
        return -1;
    return UITreeSceneBridge_EnsureFont(self->bridge, font_id);
}

static void
upload_model_nodes(
    struct UITree* tree,
    struct UITreeSceneBridge* bridge)
{
    uint32_t i;
    assert(tree && bridge);
    for( i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent* c = &tree->components[i];
        int cache_id;
        int scene_id;
        if( c->type != UIELEM_RS_MODEL )
            continue;
        cache_id = c->u.rs_model.gamecache_model_id;
        if( cache_id < 0 )
        {
            /* Local-player / player-design preview: composite the default avatar. */
            if( c->behavior.client_code == 327 || c->behavior.client_code == 328 )
            {
                scene_id = UITreeSceneBridge_EnsurePlayerModel(bridge);
                if( scene_id >= 0 )
                {
                    c->u.rs_model.gamecache_model_id = scene_id;
                    /* Idle the preview. The server appearance's readyanim is not
                     * decoded here, so use the standard human idle sequence; the
                     * tick driver loads it and disables gracefully if absent. */
                    if( c->u.rs_model.anim_seq_id < 0 )
                        c->u.rs_model.anim_seq_id = INTERFACE_PLAYER_IDLE_SEQ;
                }
            }
            continue;
        }
        scene_id = UITreeSceneBridge_EnsureModel(bridge, cache_id);
        if( scene_id >= 0 )
            c->u.rs_model.gamecache_model_id = scene_id;
    }
}

/*
 * Drive the player-preview components (client_code 327/328) from the player idle,
 * mirroring the TS client (widgets-gl.ts): the local-player model widget is not
 * animated by whatever sequence an onLoad script happens to set — it plays the
 * player's readyanim/idle. We have no live player entity here, so use the default
 * unarmed human idle. Runs AFTER onLoad so a spurious CC/IF_SETMODELANIM (e.g. a
 * facial/head sequence like seq 0) does not leave only the head animating.
 */
static void
reassert_player_idle_anim(struct UITree* tree)
{
    uint32_t i;
    assert(tree);
    for( i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent* c = &tree->components[i];
        if( c->type != UIELEM_RS_MODEL )
            continue;
        if( c->behavior.client_code != 327 && c->behavior.client_code != 328 )
            continue;
        if( c->u.rs_model.gamecache_model_id != UITREE_SCENE_PLAYER_MODEL_ID )
            continue;
        c->u.rs_model.anim_seq_id = INTERFACE_PLAYER_IDLE_SEQ;
        c->u.rs_model.anim_frame = 0;
        c->u.rs_model.anim_frame_cycle = 0;
    }
}

static void
collect_onloads(
    struct Task_InterfaceOpen* self,
    struct ToriRS_ComponentPack const* pack)
{
    assert(self && pack);
    self->onload_count = 0;
    for( int i = 0; i < pack->component_count; i++ )
    {
        struct ToriRS_Component const* src = &pack->components[i];
        if( src->on_load.argc <= 0 )
            continue;
        int script_id = src->on_load.argv[0];
        if( script_id <= 0 )
            continue;
        assert(self->onload_count < INTERFACE_OPEN_ONLOAD_MAX);
        struct InterfaceOpenOnLoad* hook = &self->onloads[self->onload_count++];
        hook->component_id = src->id;
        hook->script_id = script_id;
        hook->argc = src->on_load.argc;
        if( hook->argc > INTERFACE_OPEN_ONLOAD_ARGV_MAX )
        {
            fprintf(
                stderr,
                "InterfaceOpen: onload argc %d truncated to %d (component 0x%x)\n",
                hook->argc,
                INTERFACE_OPEN_ONLOAD_ARGV_MAX,
                (unsigned)src->id);
            hook->argc = INTERFACE_OPEN_ONLOAD_ARGV_MAX;
        }
        memcpy(hook->argv, src->on_load.argv, (size_t)hook->argc * sizeof(int));
    }
}

static void
collect_seed_objs(struct Task_InterfaceOpen* self)
{
    assert(self && self->invs);
    self->seed_obj_count = 0;

    static int const containers[] = {
        INV_MANAGER_CONTAINER_WORN,
        INV_MANAGER_CONTAINER_BACKPACK,
    };
    for( size_t ci = 0; ci < sizeof(containers) / sizeof(containers[0]); ci++ )
    {
        struct InvContainer const* c = InvManager_FindContainer(self->invs, containers[ci]);
        if( !c || !c->slots )
            continue;
        for( int s = 0; s < c->slot_count; s++ )
        {
            int oid = c->slots[s].obj_id;
            if( oid <= INV_MANAGER_EMPTY_OBJ_ID )
                continue;
            int found = 0;
            for( int j = 0; j < self->seed_obj_count; j++ )
            {
                if( self->seed_obj_ids[j] == oid )
                {
                    found = 1;
                    break;
                }
            }
            if( found )
                continue;
            assert(self->seed_obj_count < INTERFACE_OPEN_SEED_OBJ_MAX);
            self->seed_obj_ids[self->seed_obj_count++] = oid;
        }
    }
}

static void
layout_tree(struct Task_InterfaceOpen* self)
{
    /* Always full client canvas. Host size for openSub flows through the
     * mount parent's abs_w/h after reparent — do not shrink root_w/h or
     * toplevel + bank abs collapse to the top-left. */
    UITree_LayoutResolve(
        self->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
}

static void
mount_pack_under_target(struct Task_InterfaceOpen* self)
{
    int32_t mount_idx;
    int32_t root;
    int32_t next;
    assert(self->target_uid >= 0);
    mount_idx = UITree_FindByComponentId(self->tree, self->target_uid);
    assert(mount_idx >= 0 && "openSub target must exist");

    (void)UITree_InterfaceParentSet(
        self->tree, self->target_uid, self->interface_id, self->mount_type);

    for( root = self->tree->root_index; root >= 0; root = next )
    {
        int cid = self->tree->components[root].component_id;
        int group = (cid >> 16) & 0xffff;
        next = self->tree->components[root].next_sibling;
        if( group != self->interface_id )
            continue;
        if( self->tree->components[root].parent == mount_idx )
            continue;
        UITree_Reparent(self->tree, root, mount_idx);
    }
}

static void
hide_unmounted_spillover(struct Task_InterfaceOpen* self)
{
    int32_t root;
    for( root = self->tree->root_index; root >= 0;
         root = self->tree->components[root].next_sibling )
    {
        int cid = self->tree->components[root].component_id;
        int group = (cid >> 16) & 0xffff;
        if( cid < 0 || group <= 0 )
            continue;
        if( group == self->interface_id )
            continue;
        if( UITree_InterfaceParentIsMountedGroup(self->tree, group) )
            continue;
        /* Keep already-mounted groups and chrome that parents them. */
        if( self->tree->components[root].parent >= 0 )
            continue;
        /* Never hide the active toplevel root group (e.g. 161) while subs are
         * mounted into it — only hide accidental sibling spillover packs. */
        if( self->target_uid >= 0 )
        {
            int host_group = (self->target_uid >> 16) & 0xffff;
            if( group == host_group )
                continue;
        }
        self->tree->components[root].behavior.hide = 1;
    }
}

static void
collect_runtime_hooks_kind(
    struct Task_InterfaceOpen* self,
    int use_resize)
{
    uint32_t i;
    self->runtime_hook_count = 0;
    for( i = 0; i < self->tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &self->tree->components[i];
        struct UITreeRuntimeScriptHook const* slot =
            use_resize ? &c->runtime_hooks.on_resize : &c->runtime_hooks.on_sub_change;
        struct InterfaceOpenRuntimeHook* dst;
        if( slot->script_id <= 0 )
            continue;
        if( use_resize )
        {
            int group = (c->component_id >> 16) & 0xffff;
            if( group != self->interface_id && self->target_uid >= 0 )
                continue;
        }
        assert(self->runtime_hook_count < INTERFACE_OPEN_RUNTIME_HOOK_MAX);
        dst = &self->runtime_hooks[self->runtime_hook_count++];
        dst->component_id = c->component_id;
        dst->script_id = slot->script_id;
        dst->argc = slot->argc;
        if( dst->argc > 32 )
            dst->argc = 32;
        if( dst->argc > 0 )
            memcpy(dst->argv, slot->argv, (size_t)dst->argc * sizeof(int));
    }
}

static int
Task_InterfaceOpen_Run(
    struct ToriRS_Task* base,
    struct ToriRS_IO* io)
{
    struct Task_InterfaceOpen* self = (struct Task_InterfaceOpen*)base;
    assert(self->provider);
    assert(self->tree);
    assert(self->host);
    assert(self->invs);
    assert(self->bridge);
    assert(self->interface_id > 0);

    PT_BEGIN(&self->pt);

    /* 1. Load interface pack. */
    TASK_AWAITSELF_IF(CreateTask_ComponentPackLoad(self->provider, self->interface_id));
    assert(CacheProvider_ComponentPackHas(self->provider, self->interface_id));

    /* 2. Prefetch pack sprites / fonts / models. */
    TASK_AWAITSELF_IF(CreateTask_PackAssetsLoad(self->provider, self->interface_id));

    /* 2b. IF1 scrollbar arrow sprites (archive "scrollbar", frames 0/1). */
    TASK_AWAITSELF_IF(CreateTask_SpriteLoadByName(self->provider, "scrollbar"));
    {
        int scrollbar_id = CacheProvider_SpriteIdByName(self->provider, "scrollbar");
        if( scrollbar_id >= 0 )
            (void)UITreeSceneBridge_EnsureScrollbar(self->bridge, scrollbar_id);
    }

    /* 3. Load seeded inv objs + inventory models, then rasterize icons. */
    collect_seed_objs(self);
    TASK_AWAITSELF_IF(
        CreateTask_ObjModelLoad(self->provider, self->seed_obj_ids, NULL, self->seed_obj_count));
    {
        static int const k_containers[] = {
            INV_MANAGER_CONTAINER_WORN,
            INV_MANAGER_CONTAINER_BACKPACK,
        };
        int ci;
        for( ci = 0; ci < (int)(sizeof(k_containers) / sizeof(k_containers[0])); ci++ )
        {
            struct InvContainer* c = InvManager_GetContainer(self->invs, k_containers[ci]);
            int slot;
            if( !c )
                continue;
            for( slot = 0; slot < c->slot_count; slot++ )
            {
                int oid = c->slots[slot].obj_id;
                int count = c->slots[slot].obj_count;
                int scene_id;
                if( oid <= 0 )
                    continue;
                scene_id =
                    UITreeSceneBridge_EnsureObjIcon(self->bridge, oid, count > 0 ? count : 1);
                if( scene_id >= 0 )
                {
                    c->slots[slot].scene_id = scene_id;
                    c->slots[slot].atlas_index = 0;
                }
            }
        }
    }

    /* 4. Bake pack into UITree + collect on_load hooks (scene ids via bridge). */
    {
        struct ToriRS_ComponentPack* pack =
            CacheProvider_ComponentPackGet(self->provider, self->interface_id);
        assert(pack);
        collect_onloads(self, pack);
        (void)UITree_BuildFromComponentPack(
            self->tree, pack, open_resolve_sprite, open_resolve_font, self);
        upload_model_nodes(self->tree, self->bridge);
    }

    if( self->target_uid >= 0 )
    {
        /* Close existing mount at target, then reparent. */
        {
            int old = UITree_InterfaceParentFind(self->tree, self->target_uid);
            if( old >= 0 )
            {
                int old_group = self->tree->interface_parents[old].group_id;
                int32_t r;
                for( r = self->tree->root_index; r >= 0; r = self->tree->components[r].next_sibling )
                {
                    int cid = self->tree->components[r].component_id;
                    if( ((cid >> 16) & 0xffff) == old_group )
                        self->tree->components[r].behavior.hide = 1;
                }
                UITree_InterfaceParentClear(self->tree, self->target_uid);
            }
        }
        mount_pack_under_target(self);
    }

    /* 5. Layout at full client canvas (host size via parent abs after reparent). */
    layout_tree(self);

    /* 6. Run IF3 on_load hooks. */
    for( self->i = 0; self->i < self->onload_count; self->i++ )
    {
        struct InterfaceOpenOnLoad const* hook = &self->onloads[self->i];
        int const* args;
        int arg_count;

        self->script_id = hook->script_id;
        if( self->script_id <= 0 )
            continue;

        if( hook->argc > 1 )
        {
            args = hook->argv + 1;
            arg_count = hook->argc - 1;
        }
        else
        {
            args = NULL;
            arg_count = 0;
        }

        TASK_AWAITSELF_IF(CreateTask_CS2Run(
            self->host, self->script_id, hook->component_id, hook->component_id, args, arg_count));
    }

    /* 7. Re-layout after onLoad. */
    layout_tree(self);

    /* 8. Sub-only: onResize → layout → onSubChange. */
    if( self->target_uid >= 0 )
    {
        collect_runtime_hooks_kind(self, 1);
        for( self->i = 0; self->i < self->runtime_hook_count; self->i++ )
        {
            struct InterfaceOpenRuntimeHook const* hook = &self->runtime_hooks[self->i];
            TASK_AWAITSELF_IF(CreateTask_CS2Run(
                self->host,
                hook->script_id,
                hook->component_id,
                hook->component_id,
                hook->argc > 0 ? hook->argv : NULL,
                hook->argc));
        }
        layout_tree(self);

        collect_runtime_hooks_kind(self, 0);
        for( self->i = 0; self->i < self->runtime_hook_count; self->i++ )
        {
            struct InterfaceOpenRuntimeHook const* hook = &self->runtime_hooks[self->i];
            TASK_AWAITSELF_IF(CreateTask_CS2Run(
                self->host,
                hook->script_id,
                hook->component_id,
                hook->component_id,
                hook->argc > 0 ? hook->argv : NULL,
                hook->argc));
        }
        layout_tree(self);
    }

    /* 9. Inv + var transmit hooks registered during on_load/resize. */
    TASK_AWAITSELF_IF(CreateTask_CS2InvTransmitDispatch(self->host, -1));
    TASK_AWAITSELF_IF(CreateTask_CS2VarTransmitDispatch(self->host, -1));

    /* 9b. Player-preview components idle with the player readyanim, not whatever
     * sequence an onLoad script set (TS parity — see reassert_player_idle_anim). */
    reassert_player_idle_anim(self->tree);

    /* 10. Final layout. */
    layout_tree(self);

    /* Spillover sibling roots (not InterfaceParent-mounted) stay hidden. */
    hide_unmounted_spillover(self);

    if( self->stats )
    {
        struct ToriRS_ComponentPack* pack =
            CacheProvider_ComponentPackGet(self->provider, self->interface_id);
        self->stats->interface_id = self->interface_id;
        self->stats->pack_component_count = pack ? pack->component_count : 0;
        self->stats->onload_count = self->onload_count;
        self->stats->tree_component_count = self->tree->component_count;
    }

    PT_END(&self->pt);
}

static void
Task_InterfaceOpen_Free(struct ToriRS_Task* base)
{
    free(base);
}

static struct ToriRS_TaskVTable Task_InterfaceOpen_VTable = {
    .run = Task_InterfaceOpen_Run,
    .free = Task_InterfaceOpen_Free,
};

static struct ToriRS_Task*
create_interface_open_common(
    struct CacheProvider* provider,
    struct UITree* tree,
    struct RS_CS2Host* host,
    struct InvManager* invs,
    struct UITreeSceneBridge* bridge,
    int interface_id,
    int target_uid,
    int mount_type,
    struct InterfaceOpenStats* stats,
    char const* name)
{
    struct Task_InterfaceOpen* task;

    assert(provider);
    assert(tree);
    assert(host);
    assert(invs);
    assert(bridge);
    assert(interface_id > 0);

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_InterfaceOpen_VTable;
    strncpy(task->task.name, name, sizeof(task->task.name) - 1);
    task->provider = provider;
    task->tree = tree;
    task->host = host;
    task->invs = invs;
    task->bridge = bridge;
    task->interface_id = interface_id;
    task->target_uid = target_uid;
    task->mount_type = mount_type;
    task->stats = stats;
    PT_INIT(&task->pt);
    return &task->task;
}

struct ToriRS_Task*
CreateTask_InterfaceOpen(
    struct CacheProvider* provider,
    struct UITree* tree,
    struct RS_CS2Host* host,
    struct InvManager* invs,
    struct UITreeSceneBridge* bridge,
    int interface_id,
    struct InterfaceOpenStats* stats)
{
    return create_interface_open_common(
        provider, tree, host, invs, bridge, interface_id, -1, 0, stats, "InterfaceOpen");
}

struct ToriRS_Task*
CreateTask_InterfaceOpenSub(
    struct CacheProvider* provider,
    struct UITree* tree,
    struct RS_CS2Host* host,
    struct InvManager* invs,
    struct UITreeSceneBridge* bridge,
    int target_uid,
    int interface_id,
    int type,
    struct InterfaceOpenStats* stats)
{
    assert(target_uid >= 0);
    return create_interface_open_common(
        provider,
        tree,
        host,
        invs,
        bridge,
        interface_id,
        target_uid,
        type,
        stats,
        "InterfaceOpenSub");
}
